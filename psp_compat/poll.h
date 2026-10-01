// poll() for pspdev's newlib, which doesn't have one. The implementation is
// in poll.c.
//
// Sockets are polled with libcglue's select(). Anything else (regular files,
// which VfsPosix polls for select/asyncio) is always ready to read and write.
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

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif // PSP_COMPAT_POLL_H
