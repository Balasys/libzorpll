/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_SOCKET_H_INCLUDED
#define ZORP_SOCKET_H_INCLUDED

#include <zorpll/zorplib.h>
#include <zorpll/sockaddr.h>

#ifdef __cplusplus
extern "C" {
#endif

/** bind to the next unused port in the same group */
#define ZSF_LOOSE_BIND    0x0001
#define ZSF_ACCEPT_ONE    0x0002
#define ZSF_MARK_TPROXY   0x0004
#define ZSF_TRANSPARENT   0x0008
/** bind to a port in the same group chosen at (truly) random */
#define ZSF_RANDOM_BIND   0x0010

static inline const gchar *
z_socket_type_to_str(gint socket_type)
{
  return socket_type == SOCK_STREAM ? "stream" : 
         socket_type == SOCK_DGRAM  ? "dgram" : 
         "unknown";
}

/**
 * Type for table of socket-related functions.
 **/
typedef struct _ZSocketFuncs
{
  gint (*bind)(int fd, struct sockaddr *sa, socklen_t salen, guint32 sock_flags);
  gint (*accept)(int fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags);
  gint (*connect)(int fd, struct sockaddr *sa, socklen_t salen, guint32 sock_flags);
  gint (*listen)(int fd, gint backlog, guint32 sock_flags);
  gint (*getsockname)(int fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags);
  gint (*getpeername)(int fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags);
  gint (*getdestname)(int fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags);
} ZSocketFuncs;

gint z_do_ll_bind(int fd, struct sockaddr *sa, socklen_t salen, guint32 sock_flags);
gint z_do_ll_accept(int fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags);
gint z_do_ll_connect(int fd, struct sockaddr *sa, socklen_t salen, guint32 sock_flags);
gint z_do_ll_listen(int fd, gint backlog, guint32 sock_flags);
gint z_do_ll_getsockname(int fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags);
gint z_do_ll_getpeername(int fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags);

extern ZSocketFuncs *socket_funcs;

static inline gint
z_ll_bind(gint fd, struct sockaddr *sa, socklen_t salen, guint32 sock_flags)
{
  return socket_funcs->bind(fd, sa, salen, sock_flags);
}

static inline gint 
z_ll_accept(gint fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags)
{
  return socket_funcs->accept(fd, sa, salen, sock_flags);
}

static inline gint 
z_ll_connect(gint fd, struct sockaddr *sa, socklen_t salen, guint32 sock_flags)
{
  return socket_funcs->connect(fd, sa, salen, sock_flags);
}

static inline gint
z_ll_listen(gint fd, gint backlog, guint32 sock_flags)
{
  return socket_funcs->listen(fd, backlog, sock_flags);
}

static inline gint
z_ll_getsockname(gint fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags)
{
  return socket_funcs->getsockname(fd, sa, salen, sock_flags);
}

static inline gint
z_ll_getpeername(gint fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags)
{
  return socket_funcs->getpeername(fd, sa, salen, sock_flags);
}

static inline gint
z_ll_getdestname(gint fd, struct sockaddr *sa, socklen_t *salen, guint32 sock_flags)
{
  return socket_funcs->getdestname(fd, sa, salen, sock_flags);
}

GIOStatus z_bind(gint fd, ZSockAddr *addr, guint32 sock_flags);
GIOStatus z_bind2(gint fd, ZSockAddr *addr, guint32 sock_flags);
GIOStatus z_accept(gint fd, gint *newfd, ZSockAddr **addr, guint32 sock_flags);
GIOStatus z_connect(gint fd, ZSockAddr *remote, guint32 sock_flags);
GIOStatus z_disconnect(int fd, guint32 sock_flags);

GIOStatus z_listen(gint fd, gint backlog, guint32 sock_flags);
GIOStatus z_getsockname(gint fd, ZSockAddr **local_addr, guint32 sock_flags);
GIOStatus z_getpeername(gint fd, ZSockAddr **peer_addr, guint32 sock_flags);
GIOStatus z_getdestname(gint fd, ZSockAddr **dest_addr, guint32 sock_flags);

gboolean z_socket_init(void);
void z_socket_done(void);

#ifdef __cplusplus
}
#endif

#endif
