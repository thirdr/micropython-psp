// Blocking sockets for PPSSPP.
//
// PPSSPP doesn't implement blocking sockets yet ("workaround until
// blocking-socket" in its sceNetInet.cpp): a blocking recv() with no data
// yet returns EAGAIN, where a real PSP waits. So in the emulator only, these
// wrappers (linked with --wrap) wait with select() and retry, honouring the
// timeout the script set. On hardware they pass straight through.
//
// The socket module sets a socket's mode with fcntl(O_NONBLOCK) and its
// timeout with setsockopt(SO_RCVTIMEO / SO_SNDTIMEO), so those are wrapped
// too, to remember each socket's settings.
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <pspthreadman.h>

#include "psp_emu.h"

#define TRACKED_FDS (256)

typedef struct {
    unsigned char nonblocking;
    unsigned int recv_timeout_ms;   // 0: wait for ever
    unsigned int send_timeout_ms;
} sock_mode_t;

static sock_mode_t modes[TRACKED_FDS];

int __real_fcntl(int fd, int cmd, ...);
int __real_setsockopt(int fd, int level, int name, const void *value, socklen_t len);
ssize_t __real_recv(int fd, void *buf, size_t len, int flags);
ssize_t __real_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
ssize_t __real_send(int fd, const void *buf, size_t len, int flags);
ssize_t __real_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
int __real_accept(int fd, struct sockaddr *addr, socklen_t *len);
ssize_t __real_read(int fd, void *buf, size_t len);
ssize_t __real_write(int fd, const void *buf, size_t len);

static int emulated(void) {
    static int known = -1;
    if (known < 0) {
        known = psp_emu_is_emulator();
    }
    return known;
}

int __wrap_fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    int arg = va_arg(ap, int);
    va_end(ap);
    int r = __real_fcntl(fd, cmd, arg);
    if (r >= 0 && cmd == F_SETFL && fd >= 0 && fd < TRACKED_FDS) {
        modes[fd].nonblocking = (arg & O_NONBLOCK) != 0;
    }
    return r;
}

int __wrap_setsockopt(int fd, int level, int name, const void *value, socklen_t len) {
    int r = __real_setsockopt(fd, level, name, value, len);
    if (r == 0 && level == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO)
        && len >= sizeof(struct timeval) && fd >= 0 && fd < TRACKED_FDS) {
        const struct timeval *tv = value;
        unsigned int ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
        if (name == SO_RCVTIMEO) {
            modes[fd].recv_timeout_ms = ms;
        } else {
            modes[fd].send_timeout_ms = ms;
        }
    }
    return r;
}

// After an EAGAIN: wait until fd is readable (or writable), up to the
// socket's timeout. Returns 1 to retry, 0 to give up with EAGAIN.
static int wait_ready(int fd, int for_write, unsigned long long *start_us) {
    if (!emulated() || fd < 0 || fd >= TRACKED_FDS || modes[fd].nonblocking) {
        return 0;
    }
    unsigned int timeout_ms = for_write ? modes[fd].send_timeout_ms : modes[fd].recv_timeout_ms;
    unsigned long long now = sceKernelGetSystemTimeWide();
    if (*start_us == 0) {
        *start_us = now;
    }
    if (timeout_ms && now - *start_us >= (unsigned long long)timeout_ms * 1000) {
        return 0;
    }
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval tv = { 0, 10 * 1000 };
    if (select(fd + 1, for_write ? NULL : &set, for_write ? &set : NULL, NULL, &tv) < 0) {
        // Not a socket (read()/write() on a file never gets here normally).
        return 0;
    }
    return 1;
}

#define RETRY(call, fd, for_write) \
    unsigned long long start = 0; \
    for (;;) { \
        ssize_t r = (call); \
        if (r >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK) || !wait_ready((fd), (for_write), &start)) { \
            return r; \
        } \
    }

ssize_t __wrap_recv(int fd, void *buf, size_t len, int flags) {
    if (flags & MSG_DONTWAIT) {
        return __real_recv(fd, buf, len, flags);
    }
    RETRY(__real_recv(fd, buf, len, flags), fd, 0)
}

ssize_t __wrap_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen) {
    if (flags & MSG_DONTWAIT) {
        return __real_recvfrom(fd, buf, len, flags, from, fromlen);
    }
    RETRY(__real_recvfrom(fd, buf, len, flags, from, fromlen), fd, 0)
}

ssize_t __wrap_send(int fd, const void *buf, size_t len, int flags) {
    if (flags & MSG_DONTWAIT) {
        return __real_send(fd, buf, len, flags);
    }
    RETRY(__real_send(fd, buf, len, flags), fd, 1)
}

ssize_t __wrap_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
    if (flags & MSG_DONTWAIT) {
        return __real_sendto(fd, buf, len, flags, to, tolen);
    }
    RETRY(__real_sendto(fd, buf, len, flags, to, tolen), fd, 1)
}

int __wrap_accept(int fd, struct sockaddr *addr, socklen_t *len) {
    unsigned long long start = 0;
    for (;;) {
        int r = __real_accept(fd, addr, len);
        if (r >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK) || !wait_ready(fd, 0, &start)) {
            if (r >= 0 && r < TRACKED_FDS) {
                modes[r] = (sock_mode_t){ 0, 0, 0 };
            }
            return r;
        }
    }
}

ssize_t __wrap_read(int fd, void *buf, size_t len) {
    RETRY(__real_read(fd, buf, len), fd, 0)
}

ssize_t __wrap_write(int fd, const void *buf, size_t len) {
    RETRY(__real_write(fd, buf, len), fd, 1)
}

// A new socket starts blocking with no timeout. (A reused fd number would
// otherwise inherit the old socket's settings.)
int __real_socket(int domain, int type, int protocol);
int __wrap_socket(int domain, int type, int protocol) {
    int r = __real_socket(domain, type, protocol);
    if (r >= 0 && r < TRACKED_FDS) {
        modes[r] = (sock_mode_t){ 0, 0, 0 };
    }
    return r;
}
