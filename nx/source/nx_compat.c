/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

nx_compat.c

Weak fallback implementations of POSIX networking helpers that older libnx
sysroots may not provide. They are declared `weak` so that if the toolchain
*does* ship strong implementations, those win at link time and these are
silently discarded - i.e. this file is safe to compile unconditionally.

getifaddrs() is only reached on desktop POSIX targets (the Switch build uses
the getaddrinfo() fallback in net_ip.c), so returning "no interfaces" here is
harmless: the engine still binds sockets to INADDR_ANY.
===========================================================================
*/

#include <ifaddrs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

__attribute__((weak)) int getifaddrs(struct ifaddrs **ifap)
{
    if (ifap) {
        *ifap = 0;
    }
    return -1;
}

__attribute__((weak)) void freeifaddrs(struct ifaddrs *ifa)
{
    (void)ifa;
}

/* Legacy IPv4 string->address, missing from libnx. Implemented via the modern
   inet_pton(). Weak so a real libnx implementation wins if present. */
__attribute__((weak)) in_addr_t inet_addr(const char *cp)
{
    struct in_addr a;
    if (cp && inet_pton(AF_INET, cp, &a) == 1) {
        return a.s_addr;
    }
    return (in_addr_t)0xFFFFFFFFu; /* INADDR_NONE */
}

/* No interface enumeration on the console; index 0 == "unspecified". */
__attribute__((weak)) unsigned int if_nametoindex(const char *ifname)
{
    (void)ifname;
    return 0;
}
