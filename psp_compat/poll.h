// poll() for pspdev's newlib, which doesn't have one.
//
// MicroPython's VfsPosix only polls regular files (for select/asyncio), and
// a regular file is always ready to read and write, so this reports every
// requested event as ready.
#ifndef PSP_COMPAT_POLL_H
#define PSP_COMPAT_POLL_H

#define POLLIN   0x0001
#define POLLPRI  0x0002
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

typedef unsigned int nfds_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

static inline int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    (void)timeout;
    int ready = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = fds[i].events & (POLLIN | POLLOUT);
        if (fds[i].revents) {
            ready++;
        }
    }
    return ready;
}

#endif // PSP_COMPAT_POLL_H
