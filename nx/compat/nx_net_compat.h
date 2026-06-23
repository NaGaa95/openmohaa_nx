/*
===========================================================================
OpenMoHAA - Nintendo Switch (libnx) port

compat/nx_net_compat.h

Force-included (via -include) into qcommon/net_ip.c ONLY. libnx's BSD socket
headers are missing a few pieces that net_ip.c's IPv6 path references:

  * struct ipv6_mreq        (IPv6 multicast group membership)
  * inet_addr()             (legacy IPv4 string->addr)
  * if_nametoindex()        (interface name->index)

We pull in the system net headers first (so anything libnx DOES provide is
used as-is), then supply the missing declarations. Weak definitions live in
source/nx_compat.c. IPv6 multicast is non-functional on the console anyway;
this just lets the code compile so the IPv4 netcode works.
===========================================================================
*/

#ifndef NX_NET_COMPAT_H
#define NX_NET_COMPAT_H

#ifdef __SWITCH__

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

/* libnx does not define the IPv6 multicast membership structure. */
struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;
    unsigned int    ipv6mr_interface;
};

#ifdef __cplusplus
extern "C" {
#endif

in_addr_t    inet_addr(const char *cp);
unsigned int if_nametoindex(const char *ifname);

#ifdef __cplusplus
}
#endif

#endif /* __SWITCH__ */

#endif /* NX_NET_COMPAT_H */
