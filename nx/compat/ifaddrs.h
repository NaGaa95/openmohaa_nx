/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

compat/ifaddrs.h

Compatibility shim for <ifaddrs.h>, added to the compiler search path with
`-idirafter` (LOWEST priority). It is therefore used ONLY when the active
devkitA64/libnx sysroot does not already provide <ifaddrs.h>. Modern libnx
ships its own ifaddrs support, in which case this file is never seen.

code/qcommon/net_ip.c includes <ifaddrs.h> unconditionally in its POSIX
branch, but only *calls* getifaddrs() inside a __linux__/__APPLE__/__BSD__
guard (not taken on the Switch, which falls back to getaddrinfo()). So all we
must guarantee is that the include resolves and the type/prototypes exist.
The weak getifaddrs()/freeifaddrs() in source/nx_compat.c back these up.
===========================================================================
*/

#ifndef NX_COMPAT_IFADDRS_H
#define NX_COMPAT_IFADDRS_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ifaddrs {
    struct ifaddrs  *ifa_next;
    char            *ifa_name;
    unsigned int     ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    union {
        struct sockaddr *ifu_broadaddr;
        struct sockaddr *ifu_dstaddr;
    } ifa_ifu;
    void            *ifa_data;
};

#ifndef ifa_broadaddr
#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#endif
#ifndef ifa_dstaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr
#endif

int  getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#ifdef __cplusplus
}
#endif

#endif /* NX_COMPAT_IFADDRS_H */
