/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_SOCKADDR_H_INCLUDED
#define ZORP_SOCKADDR_H_INCLUDED

#include <zorpll/zorplib.h>

#include <sys/types.h>
#include <errno.h>
#include <sys/types.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#endif

#ifdef __cplusplus
#include <string>
extern "C" {
#endif

#define MAX_SOCKADDR_STRING	128

/* sockaddr public interface */

#define ZSA_LOCAL		0x0001 /**< within host */

typedef struct _ZSockAddrFuncs ZSockAddrFuncs;

/**
 * A class that encapsulates an (IPv4, IPv6 or Unix domain socket) address.
 *
 * Internally a struct sockaddr (from libc) is used.
 **/
typedef struct _ZSockAddr
{
  ZRefCount refcnt;
  guint32 flags;
  ZSockAddrFuncs *sa_funcs;
  int salen;
  struct sockaddr sa;
} ZSockAddr;

/**
 * ZSockAddr virtual method table type.
 **/
struct _ZSockAddrFuncs 
{
  GIOStatus (*sa_bind_prepare)   (int sock, ZSockAddr *self, guint32 sock_flags);
  GIOStatus (*sa_bind)		(int sock, ZSockAddr *self, guint32 sock_flags);
  ZSockAddr *(*sa_clone)        (ZSockAddr *self, gboolean wildcard_clone);
  gchar   *(*sa_format)         (ZSockAddr *self,   /* format to text form */
  				 gchar *text,
  				 gulong n);
  gchar   *(*sa_format_address) (ZSockAddr *self, gchar *text, gulong n);
  void     (*freefn)            (ZSockAddr *self);
  gboolean (*sa_equal)          (ZSockAddr *self, ZSockAddr *other);
};

/*
 * Map an address family to the related protocol family, currently it is 
 * a no-op as values for address and protocol families currently match,
 * at least on Linux.
 *
 * @param[in] af address family (one of AF_* constants)
 */
static inline gint 
z_map_pf(gint af)
{
  return af;
}

/* generic ZSockAddr */

gchar *z_sockaddr_format(ZSockAddr *self, gchar *text, gulong n);
gboolean z_sockaddr_equal(ZSockAddr *self, ZSockAddr *b);
ZSockAddr *z_sockaddr_ref(ZSockAddr *self);
void z_sockaddr_unref(ZSockAddr *self);
ZSockAddr *z_sockaddr_new(struct sockaddr *sa, gsize salen);

static inline const struct sockaddr *
z_sockaddr_get_sa(ZSockAddr *self)
{
  return &self->sa;
}

/* AF_INET sockaddr */

gboolean z_sockaddr_inet_check(ZSockAddr *s);

static inline const struct sockaddr_in *
z_sockaddr_inet_get_sa(ZSockAddr *s)
{
  g_assert(z_sockaddr_inet_check(s));
  
  return (struct sockaddr_in *) z_sockaddr_get_sa(s);
}

gboolean z_sockaddr_inet6_check(ZSockAddr *s);

static inline const struct sockaddr_in6 *
z_sockaddr_inet6_get_sa(ZSockAddr *s)
{
  g_assert(z_sockaddr_inet6_check(s));

  return (struct sockaddr_in6 *) z_sockaddr_get_sa(s);
}

/**
 * This ZSockAddrInet specific function returns the address part of the
 * address.
 *
 * @param[in] s ZSockAddrInet instance
 **/
static inline struct in_addr 
z_sockaddr_inet_get_address(ZSockAddr *s)
{
  g_assert(z_sockaddr_inet_check(s));
  
  return z_sockaddr_inet_get_sa(s)->sin_addr;
}

/**
 * This ZSockAddrInet6 specific function returns the address part of the
 * address.
 *
 * @param[in] s ZSockAddrInet6 instance
 **/
static inline struct in6_addr
z_sockaddr_inet6_get_address(ZSockAddr *s)
{
  g_assert(z_sockaddr_inet6_check(s));

  return z_sockaddr_inet6_get_sa(s)->sin6_addr;
}

/**
 * This ZSockAddrInet specific function sets the address part of the
 * address.
 *
 * @param[in] s ZSockAddrInet instance
 * @param[in] addr new address
 *
 **/
static inline void
z_sockaddr_inet_set_address(ZSockAddr *s, struct in_addr addr)
{
  g_assert(z_sockaddr_inet_check(s));
  
  ((struct sockaddr_in *) &s->sa)->sin_addr = addr;
}

/**
 * This ZSockAddrInet specific function returns the port part of the
 * address.
 *
 * @param[in] s ZSockAddrInet instance
 * 
 * @returns the port in host byte order
 **/
static inline guint16 
z_sockaddr_inet_get_port(ZSockAddr *s)
{
  g_assert(z_sockaddr_inet_check(s));
  
  return ntohs(z_sockaddr_inet_get_sa(s)->sin_port);
}

/**
 * This ZSockAddrInet specific function sets the port part of the
 * address.
 *
 * @param[in] s ZSockAddrInet instance
 * @param[in] port new port in host byte order
 **/
static inline void
z_sockaddr_inet_set_port(ZSockAddr *s, guint16 port)
{
  g_assert(z_sockaddr_inet_check(s));
  
  ((struct sockaddr_in *) &s->sa)->sin_port = htons(port);
}

/**
 * This ZSockAddrInet6 specific function returns the port part of the
 * address.
 *
 * @param[in] s ZSockAddrInet6 instance
 *
 * @returns the port in host byte order
 **/
static inline guint16
z_sockaddr_inet6_get_port(ZSockAddr *s)
{
  g_assert(z_sockaddr_inet6_check(s));

  return ntohs(z_sockaddr_inet6_get_sa(s)->sin6_port);
}

/**
 * This ZSockAddrInet6 specific function sets the port part of the
 * address.
 *
 * @param[in] s ZSockAddrInet6 instance
 * @param[in] port new port in host byte order
 **/
static inline void
z_sockaddr_inet6_set_port(ZSockAddr *s, guint16 port)
{
  g_assert(z_sockaddr_inet6_check(s));

  ((struct sockaddr_in6 *) &s->sa)->sin6_port = htons(port);
}

ZSockAddr *z_sockaddr_inet_new(const gchar *ip, guint16 port);
ZSockAddr *z_sockaddr_inet_new2(struct sockaddr_in *sinaddr);
ZSockAddr *z_sockaddr_inet_new_hostname(const gchar *hostname, guint16 port);
ZSockAddr *z_sockaddr_inet_range_new(const gchar *ip, guint16 min_port, guint16 max_port);
ZSockAddr *z_sockaddr_inet_range_new_inaddr(struct in_addr addr, guint16 min_port, guint16 max_port);
ZSockAddr *z_sockaddr_inet_range_new_random(const gchar *ip, guint16 min_port, guint16 max_port);
ZSockAddr *z_sockaddr_inet_range_new_inaddr_random(struct in_addr addr, guint16 min_port, guint16 max_port);

/* AF_INET6 */

ZSockAddr *z_sockaddr_inet6_new(const gchar *ip, guint16 port);
ZSockAddr *z_sockaddr_inet6_new2(struct sockaddr_in6 *sin6);

#ifndef G_OS_WIN32

gboolean z_sockaddr_unix_check(ZSockAddr *s);

static inline const gchar *
z_sockaddr_unix_get_path(ZSockAddr *s)
{
  z_sockaddr_unix_check(s);
  
  return ((struct sockaddr_un *) &s->sa)->sun_path;
}

ZSockAddr *z_sockaddr_unix_new(const gchar *name);
ZSockAddr *z_sockaddr_unix_new2(struct sockaddr_un *s_un, int sunlen);
#endif

static inline ZSockAddr *
z_sockaddr_clone(ZSockAddr *addr, gboolean wildcard_clone)
{
  return addr->sa_funcs->sa_clone(addr, wildcard_clone);
}

gchar *z_inet_ntoa(gchar *buf, size_t bufsize, struct in_addr a);
gboolean z_inet_aton(const gchar *buf, struct in_addr *a);

#ifdef __cplusplus
}

std::string z_sockaddr_format_address(ZSockAddr *self);
ZSockAddr *z_sockaddr_new_from_string(const std::string &address, guint16 port = 0);
std::string z_sockaddr_get_address(ZSockAddr *self);

#endif

#endif
