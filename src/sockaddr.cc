/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <zorpll/sockaddr.h>
#include <zorpll/log.h>
#include <zorpll/cap.h>
#include <zorpll/socket.h>
#include <zorpll/error.h>
#include <zorpll/random.h>

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>

#ifndef G_OS_WIN32
# include <netdb.h>
#endif

#ifdef G_OS_WIN32
#endif

/**
 * Thread friendly version of inet_ntoa(), converts an IP address to
 * human readable form.
 *
 * @param[out] buf store result in this buffer
 * @param[in]  bufsize the available space in buf
 * @param[in]  a address to convert.
 *
 * @returns the address of buf
 **/
gchar *
z_inet_ntoa(gchar *buf, size_t bufsize, struct in_addr a)
{
  unsigned int ip = ntohl(a.s_addr);

  g_snprintf(buf, bufsize, "%d.%d.%d.%d", 
	   (ip & 0xff000000) >> 24,
	   (ip & 0x00ff0000) >> 16,
	   (ip & 0x0000ff00) >> 8,
	   (ip & 0x000000ff));
  return buf;
}

/**
 * This function takes an IP address in text representation, parses it and 
 * returns it in a.
 *
 * @param[in]  buf buffer to parse
 * @param[out] a the parsed address
 * 
 * @returns TRUE to indicate that parsing was successful.
 **/
gboolean
z_inet_aton(const gchar *buf, struct in_addr *a)
{
#ifdef HAVE_INET_ATON
  return inet_aton(buf, a);
#else
  a->s_addr = inet_addr(buf);
  return TRUE;
#endif
}


/* general ZSockAddr functions */

#ifdef __cplusplus
ZSockAddr *
z_sockaddr_new_from_string(const std::string &address, guint16 port)
{
  GInetAddress *inet_address;
  ZSockAddr *sockaddr = nullptr;

  inet_address = g_inet_address_new_from_string(address.c_str());
  if (!inet_address)
    {
      z_log(nullptr, CORE_ERROR, 3, "Unable to parse address; address='%s'", address.c_str());
      return nullptr;
    }

  switch (g_inet_address_get_family(inet_address))
    {
#ifndef G_OS_WIN32
    case G_SOCKET_FAMILY_IPV6:
      sockaddr = z_sockaddr_inet6_new(address.c_str(), port);
      break;
#endif
    case G_SOCKET_FAMILY_IPV4:
      sockaddr = z_sockaddr_inet_new(address.c_str(), port);
      break;
#ifndef G_OS_WIN32
    case G_SOCKET_FAMILY_UNIX:
      sockaddr = z_sockaddr_unix_new(address.c_str());
      break;
#endif
    default:
      z_log(nullptr, CORE_ERROR, 3, "Unsupported socket family in z_sockaddr_new_from_string(); address='%s'", address.c_str());
      g_object_unref(inet_address);
      return nullptr;
    }

  g_object_unref(inet_address);
  return sockaddr;
}
#endif

/**
 * General function to allocate and initialize a ZSockAddr structure,
 * and convert a libc style sockaddr * pointer to our representation.
 *
 * @param[in] sa libc sockaddr * pointer to convert
 * @param[in] salen size of sa
 *
 * @returns new ZSockAddr instance
 **/
ZSockAddr *
z_sockaddr_new(struct sockaddr *sa, gsize salen)
{
  z_enter();
  switch (sa->sa_family)
    {
#ifndef G_OS_WIN32
    case AF_INET6:
      if (salen >= sizeof(struct sockaddr_in6))
        z_return(z_sockaddr_inet6_new2((struct sockaddr_in6 *) sa));
      break;
      
#endif
    case AF_INET:
      if (salen >= sizeof(struct sockaddr_in))
        z_return(z_sockaddr_inet_new2((struct sockaddr_in *) sa));
      break;
      
#ifndef G_OS_WIN32
    case AF_UNIX:
      /* NOTE: the sockaddr_un structure might be less than struct sockaddr_un */
      z_return(z_sockaddr_unix_new2((struct sockaddr_un *) sa, salen));
#endif
    default:
      /*LOG
        This message indicates an internal error, the program tried to use an
        unsupported address family. Please report this error to the Zorp QA team.
       */
      z_log(NULL, CORE_ERROR, 3, "Unsupported socket family in z_sockaddr_new(); family='%d'", sa->sa_family);
      z_return(NULL);
    }
  z_return(NULL);
}

#ifdef __cplusplus
std::string
z_sockaddr_format_address(ZSockAddr *address)
{
  gchar buffer[MAX_SOCKADDR_STRING];
  address->sa_funcs->sa_format_address(address, buffer, sizeof(buffer));

  return std::string(buffer);
}
#endif

/**
 * Format a ZSockAddr into human readable form, calls the format
 * virtual method of ZSockAddr.
 *
 * @param[in]  a instance pointer of a ZSockAddr
 * @param[out] text destination buffer
 * @param[in]  n the size of text
 *
 * @returns text is filled with a human readable representation of a, and a
 * pointer to text is returned.
 **/
gchar *
z_sockaddr_format(ZSockAddr *a, gchar *text, gulong n)
{
  return a->sa_funcs->sa_format(a, text, n);
}

/**
 * This function compares two ZSockAddr structures whether their _value_ is
 * equal, e.g.\ same IP/port.
 *
 * @param[in] a first sockaddr to compare
 * @param[in] b second sockaddr to compare
 *
 * @returns TRUE if they're equal
 **/
gboolean
z_sockaddr_equal(ZSockAddr *a, ZSockAddr *b)
{
  return a->sa.sa_family == b->sa.sa_family && a->sa_funcs->sa_equal(a, b);
}

/**
 * Increment the reference count of a ZSockAddr instance.
 *
 * @param[in] a pointer to ZSockAddr instance
 *
 * @returns the same instance
 **/
ZSockAddr *
z_sockaddr_ref(ZSockAddr *a)
{
  if (a)
    z_refcount_inc(&a->refcnt);
  return a;
}

/**
 * Decrement the reference count of a ZSockAddr instance, and free if
 * the refcnt reaches 0.
 *
 * @param[in] a ZSockAddr instance
 **/
void 
z_sockaddr_unref(ZSockAddr *a)
{
  if (a && z_refcount_dec(&a->refcnt)) 
    {
      if (!a->sa_funcs->freefn)
        g_free(a);
      else
        a->sa_funcs->freefn(a);
    }
}

/* AF_INET socket address */

/**
 * ZSockAddrInet is an implementation of the ZSockAddr interface,
 * encapsulating an IPv4 host address and port number.
 **/
typedef struct _ZSockAddrInet
{
  gint refcnt;
  guint32 flags;
  ZSockAddrFuncs *sa_funcs;
  int salen;
  struct sockaddr_in sin;
} ZSockAddrInet;

/**
 * This function is used as a bind_prepare callback for ZSockAddrInet
 * addresses.
 *
 * @param[in] sock fd of the socket to prepare for bind
 * @param     addr address where we are binding to (unused)
 * @param[in] sock_flags socket flags (ZSF_*)
 *
 * It currently enables SO_REUSEADDR to permit concurrent
 * access to the same socket.
 *
 * @returns GIOStatus to indicate success/failure
 **/
static GIOStatus
z_sockaddr_inet_bind_prepare(int sock, ZSockAddr * /* addr */, guint32 sock_flags)
{
  int tmp = 1;
  GIOStatus res = G_IO_STATUS_NORMAL;
  
  if ((sock_flags & ZSF_LOOSE_BIND) == 0)
    {
      if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &tmp, sizeof(tmp)) < 0)
        res = G_IO_STATUS_ERROR;
    }
  
  return res;
}

/**
 * This function is the clone callback for ZSockAddrInet sockets.
 *
 * @param[in] addr address to clone
 * @param[in] wildcard whether the port information is important
 *
 * It copies the sockaddr contents and nulls port if wildcard is TRUE.
 *
 * @returns the cloned address
 **/
static ZSockAddr *
z_sockaddr_inet_clone(ZSockAddr *addr, gboolean wildcard)
{
  ZSockAddrInet *self = g_new0(ZSockAddrInet, 1);
  
  memcpy(self, addr, sizeof(*self));
  self->refcnt = 1;
  if (wildcard)
    self->sin.sin_port = 0;
  
  return (ZSockAddr *) self;
}

static gchar *
z_sockaddr_inet_format_address(ZSockAddr *addr, gchar *text, gulong n)
{
  ZSockAddrInet *self = (ZSockAddrInet *)addr;
  char buf[32];

  g_snprintf(text, n, "%s", z_inet_ntoa(buf, sizeof(buf), self->sin.sin_addr));

  return text;
}

/**
 * This function is the format callback for ZSockAddrInet socket addresses
 * and returns the human readable representation of the IPv4 address.
 *
 * @param[in]  addr ZSockAddrInet instance
 * @param[out] text buffer to format this address into
 * @param[in]  n size of text
 *
 * @returns the start of the buffer
 **/
static gchar *
z_sockaddr_inet_format(ZSockAddr *addr, gchar *text, gulong n)
{
  ZSockAddrInet *self = (ZSockAddrInet *) addr;
  char buf[32];
  
  g_snprintf(text, n, "AF_INET(%s:%d)", 
	     z_inet_ntoa(buf, sizeof(buf), self->sin.sin_addr), 
             ntohs(self->sin.sin_port));
  return text;
}

/**
 * This function is the free callback fro ZSockAddrInet sockets.
 *
 * @param[in] addr ZSockAddrInet instance
 *
 * It frees ZSockAddrInet specific information and the address structure itself.
 **/
void
z_sockaddr_inet_free(ZSockAddr *addr)
{
  g_free(addr);
}

/**
 * Checks whether two addresses of the AF_INET family are equal.
 *
 * @param[in] addr the first address
 * @param[in] o the second address
 *
 * @returns TRUE if both the IP address and port are equal.
 **/
static gboolean
z_sockaddr_inet_equal(ZSockAddr *addr, ZSockAddr *o)
{
  ZSockAddrInet *self = (ZSockAddrInet *) addr;
  ZSockAddrInet *other = (ZSockAddrInet *) o;
  
  g_assert(self->sin.sin_family == AF_INET);
  g_assert(other->sin.sin_family == AF_INET);
  return self->sin.sin_addr.s_addr == other->sin.sin_addr.s_addr && self->sin.sin_port == other->sin.sin_port;
}

/**
 * ZSockAddrInet virtual methods.
 **/
static ZSockAddrFuncs inet_sockaddr_funcs = 
{
  z_sockaddr_inet_bind_prepare,
  NULL,
  z_sockaddr_inet_clone,
  z_sockaddr_inet_format,
  z_sockaddr_inet_format_address,
  z_sockaddr_inet_free,
  z_sockaddr_inet_equal
};

/**
 * This constructor allocates and initializes an IPv4 socket address.
 *
 * @param[in] ip text representation of an IP address
 * @param[in] port port number in host byte order
 *
 * @returns the new instance
 **/
ZSockAddr *
z_sockaddr_inet_new(const gchar *ip, guint16 port)
{
  ZSockAddrInet *self;
  struct in_addr netaddr;
  
  if (!z_inet_aton(ip, &netaddr))
    {
      return NULL;
    }
  
  self = g_new0(ZSockAddrInet, 1);
  self->refcnt = 1;
  self->flags = 0;
  self->salen = sizeof(struct sockaddr_in);
  self->sin.sin_family = AF_INET;
  self->sin.sin_addr = netaddr;
  self->sin.sin_port = htons(port);
  self->sa_funcs = &inet_sockaddr_funcs;
  
  return (ZSockAddr *) self;
}


/**
 * Alternate constructor for ZSockAddrInet which takes a (struct
 * sockaddr_in) structure instead of an ip+port.
 *
 * @param[in] sinaddr the sockaddr_in structure to convert
 *
 * @returns the allocated instance.
 **/
ZSockAddr *
z_sockaddr_inet_new2(struct sockaddr_in *sinaddr)
{
  ZSockAddrInet *self = g_new0(ZSockAddrInet, 1);
  
  self->refcnt = 1;
  self->flags = 0;
  self->salen = sizeof(struct sockaddr_in);
  self->sin = *sinaddr;
  self->sa_funcs = &inet_sockaddr_funcs;
  
  return (ZSockAddr *) self;
}

#ifndef  G_OS_WIN32
/**
 * Alternate constructor for ZSockAddrInet which takes a hostname+port
 * instead of a struct sockaddr_in.
 *
 * @param[in] hostname name of the host to resolve
 * @param[in] port port number in host byte order
 *
 * @returns the allocated instance.
 **/
ZSockAddr *
z_sockaddr_inet_new_hostname(const gchar *hostname, guint16 port)
{
  struct hostent hes, *he;
  char hostbuf[1024];
  char buf[32];
  int err = 0;
  int rc;
  ZSockAddr *saddr = NULL;

  rc = gethostbyname_r(hostname, &hes, hostbuf, sizeof(hostbuf), &he, &err);

  if (rc == 0 && he && he->h_addr_list[0])
    {
      z_inet_ntoa(buf, sizeof(buf), *((struct in_addr *) he->h_addr_list[0]));
      saddr = z_sockaddr_inet_new(buf, port);
    }

  return saddr;
}
#endif

/*+ Similar to ZSockAddrInet, but binds to a port in the given range +*/

/**
 * Class to store an IP address with a port range.
 *
 * @note it is assumed that it is safe to cast from ZSockAddrInetRange to
 * ZSockAddrInet
 **/
typedef struct _ZSockAddrInetRange
{
  gint refcnt;
  guint32 flags;
  ZSockAddrFuncs *sa_funcs;
  int salen;
  struct sockaddr_in sin;
  guint16 min_port, max_port, last_port;
  gboolean random;
} ZSockAddrInetRange;

static bool
z_sockaddr_inet_range_try_bind(ZSockAddrInetRange *self, int sock, guint32 sock_flags, guint16 port)
{
  /* attempt to bind */
  self->sin.sin_port = htons(port);
  if (z_ll_bind(sock, (struct sockaddr *) &self->sin, self->salen, sock_flags) == 0)
    {
      /*LOG
        This message reports that the SockAddrInetRange was successfully bound
        to the given port in the dynamic port range.
       */
      z_log(NULL, CORE_DEBUG, 6, "SockAddrInetRange, successfully bound; min_port='%d', max_port='%d', port='%d'", self->min_port, self->max_port, port);
      self->last_port = ++port;
      return true;
    }

  return false;
}

/**
 * This function is the bind callback of ZSockAddrInetRange.
 *
 * @param[in] sock socket to bind
 * @param[in] a address to bind to
 * @param[in] sock_flags socket flags (ZSF_*)
 *
 * It tries to allocate a port in the specified range.
 *
 * Returns: a GIOStatus value to indicate failure/success
 **/
static GIOStatus
z_sockaddr_inet_range_bind(int sock, ZSockAddr *a, guint32 sock_flags)
{
  ZSockAddrInetRange *self = (ZSockAddrInetRange *) a;
  guint16 port;
  
  if (self->min_port > self->max_port)
    {
      /*LOG
        This message indicates that SockAddrInetRange was given incorrect
        parameters, the allowed min_port is greater than max_port.
       */
      z_log(NULL, CORE_ERROR, 3, "SockAddrInetRange, invalid range given; min_port='%d', max_port='%d'", self->min_port, self->max_port);
      return G_IO_STATUS_ERROR;
    }
  if (self->random == FALSE || (self->max_port - self->min_port == 0))
    {
      for (port = self->last_port; port <= self->max_port; port++)
        {
          if (z_sockaddr_inet_range_try_bind(self, sock, sock_flags, port))
            return G_IO_STATUS_NORMAL;
        }
      for (port = self->min_port; port <= self->max_port; port++)
        {
          if (z_sockaddr_inet_range_try_bind(self, sock, sock_flags, port))
            return G_IO_STATUS_NORMAL;
        }
    }
  else
    {
      auto random_ports = z_random_sequence_create(self->min_port, self->max_port);
      if (random_ports.empty())
        {
          z_log(NULL, CORE_ERROR, 3, "SockAddrInetRange, there was not enough entropy to generate random;");
          return G_IO_STATUS_ERROR;
        }

      for (const auto &port : random_ports)
        {
          if (z_sockaddr_inet_range_try_bind(self, sock, sock_flags, port))
            return G_IO_STATUS_NORMAL;
        }
    }

  /*LOG
    This message reports that the SockAddRinetRange could not find any
    free port in the given range.
   */
  z_log(NULL, CORE_ERROR, 3,
        "SockAddrInetRange, could not find free port to bind; min_port='%d', max_port='%d'",
        self->min_port, self->max_port);
  self->last_port = self->min_port;
  return G_IO_STATUS_ERROR;
}

/**
 * This function is the clone callback for ZSockAddrInetRange sockets.
 *
 * @param[in] addr address to clone
 * @param     wildcard_clone whether the port information is important (unused)
 *
 * It copies the sockaddr contents.
 *
 * @returns the cloned address
 **/
static ZSockAddr *
z_sockaddr_inet_range_clone(ZSockAddr *addr, gboolean /* wildcard_clone */)
{
  ZSockAddrInetRange *self = g_new0(ZSockAddrInetRange, 1);
  
  memcpy(self, addr, sizeof(*self));
  self->refcnt = 1;
  if (self->max_port > self->min_port)
    {
      self->last_port = ((guint16) rand() % (self->max_port - self->min_port)) + self->min_port;
    }
  else if (self->max_port == self->min_port)
    {
      self->last_port = self->min_port;
    }
  
  return (ZSockAddr *) self;
}

/**
 * ZSockAddrInetRange virtual methods.
 **/
static ZSockAddrFuncs inet_range_sockaddr_funcs = 
{
  NULL,
  z_sockaddr_inet_range_bind,
  z_sockaddr_inet_range_clone,
  z_sockaddr_inet_format,
  z_sockaddr_inet_format_address,
  z_sockaddr_inet_free,
  z_sockaddr_inet_equal
};

/**
 * This function checks whether the given ZSockAddr instance is in fact a
 * ZSockAddrInet instance.
 *
 * @param[in] s ZSockAddr instance
 *
 * @returns TRUE if s is a ZSockAddrInet
 **/
gboolean 
z_sockaddr_inet_check(ZSockAddr *s)
{
  return (s->sa_funcs == &inet_sockaddr_funcs) || (s->sa_funcs == &inet_range_sockaddr_funcs);
}

/**
 * This constructor creates a new ZSockAddrInetRange instance with the
 * specified arguments. Will assign ports sequentally.
 *
 * @param[in] ip ip address in text form
 * @param[in] min_port minimal port to bind to
 * @param[in] max_port maximum port to bind to
 *
 * @returns the new ZSockAddrInetRange instance
 **/
ZSockAddr *
z_sockaddr_inet_range_new(const gchar *ip, guint16 min_port, guint16 max_port)
{
  struct in_addr netaddr;

  if (!z_inet_aton(ip, &netaddr))
    return NULL;
  
  return z_sockaddr_inet_range_new_inaddr(netaddr, min_port, max_port);
}

/**
 * This constructor creates a new ZSockAddrInetRange instance with the
 * specified arguments. Will assign ports randomly.
 *
 * @param[in] ip ip address in text form
 * @param[in] min_port minimal port to bind to
 * @param[in] max_port maximum port to bind to
 *
 * @returns the new ZSockAddrInetRange instance
 **/
ZSockAddr *
z_sockaddr_inet_range_new_random(const gchar *ip, guint16 min_port, guint16 max_port)
{
  struct in_addr netaddr;

  if (!z_inet_aton(ip, &netaddr))
    return NULL;

  return z_sockaddr_inet_range_new_inaddr_random(netaddr, min_port, max_port);
}

/**
 * This constructor creates a new ZSockAddrInetRange instance with the
 * specified arguments. Will assignports randomly.
 *
 * @param[in] addr address
 * @param[in] min_port minimal port to bind to
 * @param[in] max_port maximum port to bind to
 *
 * @returns the new ZSockAddr instance
 **/
ZSockAddr *
z_sockaddr_inet_range_new_inaddr_random(struct in_addr addr, guint16 min_port, guint16 max_port)
{
  ZSockAddrInetRange *self = (ZSockAddrInetRange*) z_sockaddr_inet_range_new_inaddr(addr, min_port, max_port);
  self->random = TRUE;

  return (ZSockAddr *)self;
}

/**
 * This constructor creates a new ZSockAddrInetRange instance with the
 * specified arguments. Will assign ports sequentally.
 *
 * @param[in] addr address
 * @param[in] min_port minimal port to bind to
 * @param[in] max_port maximum port to bind to
 *
 * @returns the new ZSockAddr instance
 **/
ZSockAddr *
z_sockaddr_inet_range_new_inaddr(struct in_addr addr, guint16 min_port, guint16 max_port)
{
  ZSockAddrInetRange *self;
  
  self = g_new0(ZSockAddrInetRange, 1);
  self->refcnt = 1;
  self->flags = 0;
  self->salen = sizeof(struct sockaddr_in);
  self->sin.sin_family = AF_INET;
  self->sin.sin_addr = addr;
  self->sin.sin_port = 0;
  self->sa_funcs = &inet_range_sockaddr_funcs;
  if (max_port > min_port)
    {
      self->last_port = (guint16)(rand() % (max_port - min_port)) + min_port;
    }
  else if (max_port == min_port)
    {
      self->last_port = min_port;
    }
  self->min_port = min_port;
  self->max_port = max_port;
  self->random = FALSE;
  
  return (ZSockAddr *) self;
}

#ifndef G_OS_WIN32

/**
 * ZSockAddrInet6 is an implementation of the ZSockAddr interface,
 * encapsulating an IPv6 host address and port number.
 **/
typedef struct _ZSockAddrInet6
{
  gint refcnt;
  guint32 flags;
  ZSockAddrFuncs *sa_funcs;
  int salen;
  struct sockaddr_in6 sin6;
} ZSockAddrInet6;

/**
 * This function is the clonse callback for ZSockAddrInet sockets.
 *
 * @param[in] addr address to clone
 * @param[in] wildcard whether the port information is important
 *
 * It copies the sockaddr contents and nulls port if wildcard is TRUE.
 *
 * @returns the cloned address
 **/
static ZSockAddr *
z_sockaddr_inet6_clone(ZSockAddr *addr, gboolean wildcard)
{
  ZSockAddrInet6 *self = g_new0(ZSockAddrInet6, 1);
  
  memcpy(self, addr, sizeof(*self));
  self->refcnt = 1;
  if (wildcard)
    self->sin6.sin6_port = 0;
  
  return (ZSockAddr *) self;
}

static gchar *
z_sockaddr_inet6_format_address(ZSockAddr *addr, gchar *text, gulong n)
{
  ZSockAddrInet6 *self = (ZSockAddrInet6 *)addr;
  char buf[64];

  inet_ntop(AF_INET6, &self->sin6.sin6_addr, buf, sizeof(buf));
  g_snprintf(text, n, "%s", buf);

  return text;
}

/**
 * Format an IPv6 address into human readable form.
 *
 * @param[in]  addr the address
 * @param[out] text buffer to receive the result
 * @param[in]  n length of buffer
 * 
 * @returns a pointer to text
 **/
static gchar *
z_sockaddr_inet6_format(ZSockAddr *addr, gchar *text, gulong n)
{
  ZSockAddrInet6 *self = (ZSockAddrInet6 *) addr;
  char buf[64];

  inet_ntop(AF_INET6, &self->sin6.sin6_addr, buf, sizeof(buf));
  g_snprintf(text, n, "AF_INET6(%s:%d)",
             buf,
             htons(self->sin6.sin6_port));
  return text;
}

/**
 * Frees a ZSockAddrInet6 (or ZSockAddr) instance.
 *
 * @param[in] addr ZSockAddr instance
 **/
static void
z_sockaddr_inet6_free(ZSockAddr *addr)
{
  g_free(addr);
}

/**
 * Checks whether two IPv6 addresses of are equal.
 *
 * @param[in] addr the first address
 * @param[in] o the second address
 *
 * @returns TRUE if both the IP address and port are equal.
 **/
static gboolean
z_sockaddr_inet6_equal(ZSockAddr *addr, ZSockAddr *o)
{
  ZSockAddrInet6 *self = (ZSockAddrInet6 *) addr;
  ZSockAddrInet6 *other = (ZSockAddrInet6 *) o;
  
  g_assert(self->sin6.sin6_family == AF_INET6);
  g_assert(other->sin6.sin6_family == AF_INET6);
  return memcmp(&self->sin6.sin6_addr, &other->sin6.sin6_addr, sizeof(self->sin6.sin6_addr)) == 0 && self->sin6.sin6_port == other->sin6.sin6_port;
}

/**
 * ZSockAddrInet6 virtual methods.
 **/
static ZSockAddrFuncs inet6_sockaddr_funcs =
{
  z_sockaddr_inet_bind_prepare,
  NULL,
  z_sockaddr_inet6_clone,
  z_sockaddr_inet6_format,
  z_sockaddr_inet6_format_address,
  z_sockaddr_inet6_free,
  z_sockaddr_inet6_equal
};

/**
 * Allocate and initialize an IPv6 socket address.
 *
 * @param[in] ip text representation of an IP address
 * @param[in] port port number in host byte order
 *
 * @returns ZSockAddrInet6 instance
 **/
ZSockAddr *
z_sockaddr_inet6_new(const gchar *ip, guint16 port)
{
  ZSockAddrInet6 *addr = g_new0(ZSockAddrInet6, 1);

  addr->refcnt = 1;
  addr->flags = 0;
  addr->salen = sizeof(struct sockaddr_in6);
  addr->sin6.sin6_family = AF_INET6;
  inet_pton(AF_INET6, ip, &addr->sin6.sin6_addr);
  addr->sin6.sin6_port = htons(port);
  addr->sa_funcs = &inet6_sockaddr_funcs;

  return (ZSockAddr *) addr;
}

/**
 * Allocate and initialize an IPv6 socket address using libc sockaddr *
 * structure.
 *
 * @param[in] sin6 the sockaddr_in6 structure to convert
 *
 * @returns ZSockAddrInet6 instance
 **/
ZSockAddr *
z_sockaddr_inet6_new2(struct sockaddr_in6 *sin6)
{
  ZSockAddrInet6 *addr = g_new0(ZSockAddrInet6, 1);

  addr->refcnt = 1;
  addr->flags = 0;
  addr->salen = sizeof(struct sockaddr_in6);
  addr->sin6 = *sin6;
  addr->sa_funcs = &inet6_sockaddr_funcs;

  return (ZSockAddr *) addr;
}

/**
 * This function checks whether the given ZSockAddr instance is in fact a
 * ZSockAddrInet instance.
 *
 * @param[in] s ZSockAddr instance
 *
 * @returns TRUE if s is a ZSockAddrInet
 **/
gboolean
z_sockaddr_inet6_check(ZSockAddr *s)
{
  return (s->sa_funcs == &inet6_sockaddr_funcs);
}

#endif

#ifndef G_OS_WIN32
/* AF_UNIX socket address */

/**
 * The ZSockAddrUnix class is an implementation of the ZSockAddr
 * interface encapsulating AF_UNIX domain socket names.
 **/
typedef struct _ZSockAddrUnix
{
  gint refcnt;
  guint32 flags;
  ZSockAddrFuncs *sa_funcs;
  int salen;
  struct sockaddr_un saun;
} ZSockAddrUnix;

static GIOStatus z_sockaddr_unix_bind_prepare(int sock, ZSockAddr *addr, guint32 sock_flags);
static gchar *z_sockaddr_unix_format_address(ZSockAddr *addr, gchar *text, gulong n);
static gchar *z_sockaddr_unix_format(ZSockAddr *addr, gchar *text, gulong n);

/**
 * This function is the clone callback for ZSockAddrUnix instances.
 *
 * @param[in] addr ZSockAddr instance to clone
 * @param     wildcard_clone specifies whether the clone can be a wildcard (unused)
 *
 * It copies the address information and returns a newly constructed, independent copy.
 * 
 * @returns the cloned instance
 **/
static ZSockAddr *
z_sockaddr_unix_clone(ZSockAddr *addr, gboolean /* wildcard_clone */)
{
  ZSockAddrUnix *self = g_new0(ZSockAddrUnix, 1);
  
  memcpy(self, addr, sizeof(*self));
  self->refcnt = 1;

  return (ZSockAddr *) self;
}

/**
 * Checks whether two UNIX addresses of are equal.
 *
 * @param[in] addr the first address
 * @param[in] o the second address
 *
 * @returns TRUE if the addresses are equal.
 **/
static gboolean
z_sockaddr_unix_equal(ZSockAddr *addr, ZSockAddr *o)
{
  ZSockAddrUnix *self = (ZSockAddrUnix *) addr;
  ZSockAddrUnix *other = (ZSockAddrUnix *) o;
  
  g_assert(self->saun.sun_family == AF_UNIX);
  g_assert(other->saun.sun_family == AF_UNIX);
  return strncmp(self->saun.sun_path, other->saun.sun_path, sizeof(self->saun.sun_path)) == 0;
}

/**
 * ZSockAddrUnix virtual methods.
 **/
static ZSockAddrFuncs unix_sockaddr_funcs = 
{
  z_sockaddr_unix_bind_prepare,
  NULL,
  z_sockaddr_unix_clone,
  z_sockaddr_unix_format,
  z_sockaddr_unix_format_address,
  NULL,
  z_sockaddr_unix_equal
};

/* anonymous if name == NULL */

/**
 * This function checks whether the given ZSockAddr instance is in fact a
 * ZSockAddrUnix instance.
 *
 * @param[in] s ZSockAddr instance
 *
 * @returns TRUE if s is a ZSockAddrUnix
 **/

gboolean
z_sockaddr_unix_check(ZSockAddr *s)
{
  return s->sa_funcs == &unix_sockaddr_funcs;
}

/**
 * Allocate and initialize a ZSockAddrUnix instance.
 *
 * @param[in] name filename
 *
 * @returns a ZSockAddrUnix instance
 **/
ZSockAddr *
z_sockaddr_unix_new(const gchar *name)
{
  ZSockAddrUnix *self = g_new0(ZSockAddrUnix, 1);
  
  self->refcnt = 1;
  self->flags = 0;
  self->sa_funcs = &unix_sockaddr_funcs;
  self->saun.sun_family = AF_UNIX;
  if (name)
    {
      g_strlcpy(self->saun.sun_path, name, sizeof(self->saun.sun_path));
      self->salen = sizeof(self->saun) - sizeof(self->saun.sun_path) + strlen(self->saun.sun_path) + 1;
    }
  else
    {
      self->saun.sun_path[0] = 0;
      self->salen = 2;
    }
  return (ZSockAddr *) self;
}

/**
 * Allocate and initialize a ZSockAddrUnix instance, using libc
 * sockaddr_un structure.
 *
 * @param[in] saun sockaddr_un structure to convert
 * @param[in] sunlen size of saun
 *
 * @returns a ZSockAddrUnix instance
 **/
ZSockAddr *
z_sockaddr_unix_new2(struct sockaddr_un *saun, int sunlen)
{
  ZSockAddrUnix *self = g_new0(ZSockAddrUnix, 1);
  
  self->refcnt = 1;
  self->flags = 0;
  self->sa_funcs = &unix_sockaddr_funcs;
  memset(&self->saun, 0, sizeof(self->saun));
  if (sunlen)
    {
      memcpy(&self->saun, saun, sunlen);
    }
  else
    {
      self->saun.sun_family = AF_UNIX;
    }
  self->salen = sizeof(struct sockaddr_un);
  return (ZSockAddr *) self;
}


/**
 * This function is used as a bind_prepare callback for ZSockAddrUnix
 * addresses. 
 *
 * @param     sock fd of the socket to prepare for bind (not used)
 * @param[in] addr address where we are binding to
 * @param     sock_flags socket flags (ZSF_*) (unused)
 *
 * It currently unlinks the socket if it exists and is a socket.
 *
 * @returns GIOStatus to indicate success/failure
 **/
static GIOStatus
z_sockaddr_unix_bind_prepare(int /* sock */, ZSockAddr *addr, guint32 /* sock_flags */)
{
  ZSockAddrUnix *self = (ZSockAddrUnix *) addr;
  struct stat st;
  
  if (self->saun.sun_path[0] == 0)
    return G_IO_STATUS_ERROR;

  if (stat(self->saun.sun_path, &st) == -1 ||
      !S_ISSOCK(st.st_mode))
    return G_IO_STATUS_ERROR;
  
  unlink(self->saun.sun_path);  
  return G_IO_STATUS_NORMAL;
}

static gchar *
z_sockaddr_unix_format_address(ZSockAddr *addr, gchar *text, gulong n)
{
  ZSockAddrUnix *self = (ZSockAddrUnix *) addr;

  g_snprintf(text, n, "%s", 
             self->salen > 2 && self->saun.sun_path[0] ? 
             self->saun.sun_path : "anonymous");
  return text;
}

/**
 * This function is the format callback for ZSockAddrUnix socket addresses
 * and returns the human readable representation of the unix domain socket
 * address.
 *
 * @param[in]  addr ZSockAddrUnix instance
 * @param[out] text buffer to format this address into
 * @param[in]  n size of text
 *
 * @returns the start of the buffer
 **/
gchar *
z_sockaddr_unix_format(ZSockAddr *addr, gchar *text, gulong n)
{
  ZSockAddrUnix *self = (ZSockAddrUnix *) addr;
  
  g_snprintf(text, n, "AF_UNIX(%s)", 
             self->salen > 2 && self->saun.sun_path[0] ? 
             self->saun.sun_path : "anonymous");
  return text;
}

#endif
