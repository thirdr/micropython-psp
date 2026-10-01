// poll() over libcglue's select(). See poll.h.
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

#include "poll.h"

// True if select() accepts fd, i.e. it's a socket.
static int is_socket(int fd) {
    if (fd < 0 || fd >= FD_SETSIZE) {
        return 0;
    }
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval zero = { 0, 0 };
    return select(fd + 1, &set, NULL, NULL, &zero) >= 0;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    fd_set rd, wr;
    FD_ZERO(&rd);
    FD_ZERO(&wr);
    int maxfd = -1, ready = 0;
    for (nfds_t i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (fds[i].fd < 0) {
            // Unlike POSIX (which ignores negative fds): only a closed socket
            // or file reaches here with -1, so report it as invalid.
            fds[i].revents = POLLNVAL;
            ready++;
            continue;
        }
        if (!is_socket(fds[i].fd)) {
            fds[i].revents = fds[i].events & (POLLIN | POLLOUT);
            ready += fds[i].revents != 0;
            continue;
        }
        if (fds[i].events & POLLIN) {
            FD_SET(fds[i].fd, &rd);
        }
        if (fds[i].events & POLLOUT) {
            FD_SET(fds[i].fd, &wr);
        }
        if (fds[i].fd > maxfd) {
            maxfd = fds[i].fd;
        }
    }
    if (maxfd < 0) {
        return ready;
    }
    // Something's already ready (a file): just check the sockets.
    struct timeval tv, *tvp = NULL;
    if (ready > 0 || timeout >= 0) {
        int ms = ready > 0 ? 0 : timeout;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        tvp = &tv;
    }
    // No exception set: a socket error shows up on the next recv()/send().
    int n = select(maxfd + 1, &rd, &wr, NULL, tvp);
    if (n < 0) {
        return ready > 0 ? ready : -1;
    }
    if (n == 0) {
        // libcglue leaves the sets as they were when nothing is ready.
        return ready;
    }
    for (nfds_t i = 0; i < nfds; i++) {
        int fd = fds[i].fd;
        if (fd < 0 || fd > maxfd || !(FD_ISSET(fd, &rd) || FD_ISSET(fd, &wr))) {
            continue;
        }
        if (FD_ISSET(fd, &rd)) {
            fds[i].revents |= POLLIN;
        }
        if (FD_ISSET(fd, &wr)) {
            fds[i].revents |= POLLOUT;
        }
        ready += fds[i].revents != 0;
    }
    return ready;
}
