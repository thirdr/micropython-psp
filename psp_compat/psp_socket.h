// What MicroPython's POSIX socket module (ports/unix/modsocket.c) needs that
// the pspdev socket headers lack: getaddrinfo() and friends, and the IPv6
// address types it names. The PSP's network stack is IPv4 only, so these
// handle AF_INET; IPv6 requests fail cleanly. (inet_pton() and inet_ntop()
// are the firmware's, through <arpa/inet.h>.)
//
// Force-included into modsocket.c (see ports/psp/CMakeLists.txt); the
// functions are in psp_socket.c.
#ifndef PSP_COMPAT_PSP_SOCKET_H
#define PSP_COMPAT_PSP_SOCKET_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE      0x0001
#define AI_CANONNAME    0x0002
#define AI_NUMERICHOST  0x0004
#define AI_NUMERICSERV  0x0400

#define EAI_NONAME      (-2)
#define EAI_FAIL        (-4)
#define EAI_FAMILY      (-6)
#define EAI_SERVICE     (-8)
#define EAI_MEMORY      (-10)

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
const char *gai_strerror(int code);

#define INET_ADDRSTRLEN     16
#define INET6_ADDRSTRLEN    46

struct in6_addr {
    unsigned char s6_addr[16];
};

struct sockaddr_in6 {
    unsigned char sin6_len;
    unsigned char sin6_family;
    unsigned short sin6_port;
    unsigned int sin6_flowinfo;
    struct in6_addr sin6_addr;
    unsigned int sin6_scope_id;
};

#include <arpa/inet.h>

// From ports/unix/mphalport.h, which modsocket.c expects to be the HAL.
#define RAISE_ERRNO(err_flag, error_val) \
    { if (err_flag == -1) \
      { mp_raise_OSError(error_val); } }

#endif // PSP_COMPAT_PSP_SOCKET_H
