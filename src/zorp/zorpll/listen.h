/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_LISTEN_H_INCLUDED
#define ZORP_LISTEN_H_INCLUDED

#include <zorpll/zorplib.h>
#include <zorpll/sockaddr.h>
#include <zorpll/socket.h>
#include <zorpll/zobject.h>
#include <zorpll/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef gboolean (*ZAcceptFunc)(ZStream *fdstream, ZSockAddr *client, ZSockAddr *dest, gpointer user_data);


/**
 * Listen on the given socket address, and call a given callback for
 * each incoming connection.
 **/
typedef struct _ZListener
{
  ZObject super;
  /** user request bind address */
  ZSockAddr *bind_addr;
  /** address we bound to */
  ZSockAddr *local;
  gint fd;
  
  /* these are private */
  GSource *watch;
  ZAcceptFunc callback;
  gpointer user_data;
  guint32 sock_flags;
  GRecMutex lock;
  GMainContext *context;
  gchar *session_id;
} ZListener;

/**
 * ZListener virtual method table type.
 **/
typedef struct _ZListenerFuncs 
{
  ZObjectFuncs super;
  gint (*open_listener)(ZListener *self);
  GIOStatus (*accept_connection)(ZListener *self, ZStream **fdstream, ZSockAddr **client, ZSockAddr **dest);
} ZListenerFuncs;

extern ZClass ZListener__class;

ZListener *
z_listener_new(ZClass *class_,
               const gchar *session_id,
               ZSockAddr *local,
               guint32 sock_flags,
               ZAcceptFunc callback,
               gpointer user_data);

/**
 * Increment reference count and return a reference.
 *
 * @param[in] self ZListener instance
 *
 * @returns self
 **/
static inline ZListener *
z_listener_ref(ZListener *self)
{
  return Z_CAST(z_object_ref(&self->super), ZListener);
}

/**
 * Decrement reference count.
 *
 * @param[in] self ZListener instance
 **/
static inline void
z_listener_unref(ZListener *self)
{
  z_object_unref(&self->super);
}

gboolean z_listener_start(ZListener *self) G_GNUC_WARN_UNUSED_RESULT;
gboolean z_listener_start_in_context(ZListener *self, GMainContext *context) G_GNUC_WARN_UNUSED_RESULT;
void z_listener_cancel(ZListener *self);

void z_listener_suspend(ZListener *self);
void z_listener_resume(ZListener *self);

/**
 * Get session id.
 *
 * @param[in] self ZListener instance
 * 
 * @returns session id
 **/
static inline const gchar *
z_listener_get_session_id(ZListener *self)
{
  return self->session_id;
}

gboolean z_listener_open(ZListener *s) G_GNUC_WARN_UNUSED_RESULT;

ZListener *
z_stream_listener_new(const gchar *session_id,
                      ZSockAddr *local,
                      guint32 sock_flags,
                      gint backlog,
                      ZAcceptFunc callback,
                      gpointer user_data);

#ifdef __cplusplus
}
#endif

#endif
