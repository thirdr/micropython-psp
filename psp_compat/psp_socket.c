// getaddrinfo() for the PSP, IPv4 only. See psp_socket.h.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include "psp_socket.h"

// "a.b.c.d" to 4 bytes in network order. Returns 1, or 0 if it isn't one.
static int parse_ipv4(const char *s, unsigned char *out) {
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') {
            return 0;
        }
        int v = 0;
        for (int digits = 0; *s >= '0' && *s <= '9'; s++, digits++) {
            v = v * 10 + (*s - '0');
            if (v > 255 || digits == 3) {
                return 0;
            }
        }
        out[i] = v;
        if (i < 3 && *s++ != '.') {
            return 0;
        }
    }
    return *s == '\0';
}

// One result: an IPv4 address and port, laid out as one allocation.
typedef struct {
    struct addrinfo info;
    struct sockaddr_in addr;
} result_t;

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int flags = hints ? hints->ai_flags : 0;
    if (family != AF_UNSPEC && family != AF_INET) {
        return EAI_FAMILY;
    }
    unsigned port = 0;
    if (service != NULL) {
        char *end;
        unsigned long p = strtoul(service, &end, 10);
        if (*service == '\0' || *end != '\0' || p > 65535) {
            return EAI_SERVICE;   // no services database: numbers only
        }
        port = p;
    }
    unsigned char ip[4] = { 0, 0, 0, 0 };
    if (node == NULL) {
        if (!(flags & AI_PASSIVE)) {
            ip[0] = 127, ip[3] = 1;
        }
    } else if (!parse_ipv4(node, ip)) {
        if (flags & AI_NUMERICHOST) {
            return EAI_NONAME;
        }
        struct hostent *h = gethostbyname(node);
        if (h == NULL || h->h_addrtype != AF_INET || h->h_addr_list[0] == NULL) {
            return EAI_NONAME;
        }
        memcpy(ip, h->h_addr_list[0], 4);
    }
    result_t *r = calloc(1, sizeof(result_t));
    if (r == NULL) {
        return EAI_MEMORY;
    }
    r->addr.sin_len = sizeof(struct sockaddr_in);
    r->addr.sin_family = AF_INET;
    r->addr.sin_port = htons(port);
    memcpy(&r->addr.sin_addr, ip, 4);
    r->info.ai_family = AF_INET;
    r->info.ai_socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    r->info.ai_protocol = hints ? hints->ai_protocol : 0;
    r->info.ai_addrlen = sizeof(struct sockaddr_in);
    r->info.ai_addr = (struct sockaddr *)&r->addr;
    *res = &r->info;
    return 0;
}

void freeaddrinfo(struct addrinfo *res) {
    free(res);   // result_t starts with its addrinfo
}

const char *gai_strerror(int code) {
    switch (code) {
        case EAI_NONAME:
            return "name not found";
        case EAI_FAMILY:
            return "address family not supported";
        case EAI_SERVICE:
            return "service not supported";
        case EAI_MEMORY:
            return "out of memory";
        default:
            return "address lookup failed";
    }
}
