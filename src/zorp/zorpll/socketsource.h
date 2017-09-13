/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_SOCKET_SOURCE_H_INCLUDED
#define ZORP_SOCKET_SOURCE_H_INCLUDED

#include <zorpll/stream.h>
#include <zorpll/zorplib.h>
#include <zorpll/io.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#  define Z_SOCKEVENT_READ    FD_READ
#  define Z_SOCKEVENT_WRITE   FD_WRITE
#  define Z_SOCKEVENT_PRI     FD_PRI
#  define Z_SOCKEVENT_ACCEPT  FD_ACCEPT 
#  define Z_SOCKEVENT_CONNECT FD_CONNECT
#  define Z_SOCKEVENT_HUP     FD_CLOSE
#else
#  define Z_SOCKEVENT_READ    G_IO_IN
#  define Z_SOCKEVENT_WRITE   G_IO_OUT
#  define Z_SOCKEVENT_PRI     G_IO_PRI
#  define Z_SOCKEVENT_ACCEPT  G_IO_IN
#  define Z_SOCKEVENT_CONNECT G_IO_OUT
#  define Z_SOCKEVENT_HUP     G_IO_HUP
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Event source class for sockets.
 **/
typedef struct _ZSocketSource
{
  GSource super;
  GIOCondition cond;
  GPollFD poll;
  gint timeout_time;
  gboolean suspended;
  gboolean timed_out;
#ifdef G_OS_WIN32
  SOCKET fd;
  gboolean acceptevent;
#endif
} ZSocketSource;

static inline gboolean
z_socket_source_is_suspended(GSource *s)
{
  return ((ZSocketSource *) s)->suspended;
}

typedef gboolean (*ZSocketSourceFunc)(gboolean timed_out, gpointer data);

GSource *z_socket_source_new(gint fd, GIOCondition cond, gint timeout);
void z_socket_source_suspend(GSource *);
void z_socket_source_resume(GSource *);

#ifdef __cplusplus
}
#endif

#endif
