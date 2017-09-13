/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_IO_H_INCLUDED
#define ZORP_IO_H_INCLUDED

#include <zorpll/zorplib.h>
#include <zorpll/sockaddr.h>

#ifdef __cplusplus
extern "C" {
#endif

extern GSourceFuncs z_io_watch_funcs;

gboolean z_fd_set_nonblock(int fd, gboolean enable);
gboolean z_fd_set_keepalive(int fd, gboolean enable);
gboolean z_fd_set_oobinline(int fd, gboolean enable);
void z_fd_set_our_mark(int fd, int mark);
gboolean z_fd_set_nodelay(int fd, gboolean enable);

#if ZORPLIB_ENABLE_TOS

void z_fd_get_peer_tos(gint fd, guint8 *tos);
void z_fd_get_our_tos(gint fd, guint8 *tos);
void z_fd_set_our_tos(gint fd, guint8 tos);

#else

#define z_fd_get_peer_tos(fd, tos) {}
#define z_fd_get_our_tos(fd, tos) {}
#define z_fd_set_our_tos(fd, tos) {}

#endif

#ifdef __cplusplus
}
#endif

#endif
