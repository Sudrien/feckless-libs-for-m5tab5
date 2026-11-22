/* Test stand-in (u-feckless-0071): lwIP's socket names over the host's own
 * sockets (fake_net.c), so box_net.c is tested on real loopback ones. */
#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
int lwip_socket(int domain, int type, int protocol);
int lwip_bind(int s, const struct sockaddr *name, socklen_t namelen);
int lwip_connect(int s, const struct sockaddr *name, socklen_t namelen);
int lwip_listen(int s, int backlog);
int lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
ssize_t lwip_send(int s, const void *data, size_t size, int flags);
ssize_t lwip_sendto(int s, const void *data, size_t size, int flags, const struct sockaddr *to, socklen_t tolen);
ssize_t lwip_recv(int s, void *mem, size_t len, int flags);
ssize_t lwip_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int lwip_getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen);
int lwip_getsockname(int s, struct sockaddr *name, socklen_t *namelen);
int lwip_getpeername(int s, struct sockaddr *name, socklen_t *namelen);
int lwip_shutdown(int s, int how);
int lwip_close(int s);
int lwip_fcntl(int s, int cmd, int val);
int lwip_select(int maxfdp1, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv);
const char *lwip_inet_ntop(int af, const void *src, char *dst, socklen_t size);
int lwip_inet_pton(int af, const char *src, void *dst);
