/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_CONNECT_H_INCLUDED
#define ZORP_CONNECT_H_INCLUDED


#include <zorpll/zorplib.h>
#include <zorpll/sockaddr.h>
#include <zorpll/socket.h>
#include <zorpll/zobject.h>
#include <zorpll/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/** z_io_connect public interface */

typedef void (*ZConnectFunc)(ZStream *fdstream, GError *error, gpointer user_data);


/**
 * Connect to the given socket address using the given local address,
 * and call a given callback function if the connection is established.
 **/
typedef struct _ZConnector 
{
  ZObject super;
  ZSockAddr *local;
  gint fd;
  
  /* private */
  ZSockAddr *remote;
  
  /* we use a reference to our GSource, as using source_id would cause a race */
  GSource *watch;
  gint timeout;
  ZConnectFunc callback;
  gpointer user_data;
  GDestroyNotify destroy_data;
  gint refcnt;
  GRecMutex lock;
  GMainContext *context;
  gboolean blocking;
  gint socket_type;
  guint32 sock_flags;
  gchar *session_id;
} ZConnector;

/**
 * ZConnector virtual method table type.
 **/
typedef struct _ZConnectorFuncs 
{
  ZObjectFuncs super;
} ZConnectorFuncs;

LIBZORPLL_EXTERN ZClass ZConnector__class;
LIBZORPLL_EXTERN ZClass ZStreamConnector__class;

ZConnector *
z_connector_new(ZClass *_class,
                const gchar *session_id,
                gint socket_type,
                ZSockAddr *local, 
                ZSockAddr *remote,
                guint32 sock_flags,
		ZConnectFunc callback,
		gpointer user_data,
		GDestroyNotify destroy_data);

gboolean z_connector_start_block(ZConnector *self, ZSockAddr **local, ZStream **stream);
gboolean z_connector_start(ZConnector *self, ZSockAddr **local);
gboolean z_connector_start_in_context(ZConnector *self, GMainContext *context, ZSockAddr **local);
void z_connector_set_timeout(ZConnector *self, gint timeout);
void z_connector_set_tos(ZConnector *self, gint tos);
void z_connector_set_mark(ZConnector *self, int mark);
void z_connector_cancel(ZConnector *self);

/**
 * Increment reference count and return a reference to a ZConnector.
 *
 * @param[in] self ZConnector instance
 *
 * @returns self
 **/
static inline ZConnector *
z_connector_ref(ZConnector *self)
{
  return Z_CAST(z_object_ref(&self->super), ZConnector);
}
 
/**
 * Decrement reference count of a ZConnector.
 *
 * @param[in] self ZConnector instance.
 **/
static inline void 
z_connector_unref(ZConnector *self)
{
  z_object_unref(&self->super);
}

/**
 * Get session id.
 *
 * @param[in] self ZConnector instance
 *
 * @returns session id
 **/
static inline const gchar *
z_connector_get_session_id(ZConnector *self)
{
  return self->session_id;
}

/**
 * Create a new ZStreamConnector instance.
 *
 * @param[in]      session_id session id used for logging
 * @param[in]      local local address to bind to.
 * @param[in]      remote remote address to connect to.
 * @param[in]      sock_flags socket flags
 * @param[in]      callback function to call when the connection is established.
 * @param[in]      user_data opaque pointer to pass to callback.
 * @param[in]      destroy_data destroy callback for user_data
 *
 * @returns The allocated instance.
 **/
static inline ZConnector *
z_stream_connector_new(const gchar *session_id,
                       ZSockAddr *local, 
                       ZSockAddr *remote,
                       guint32 sock_flags,
                       ZConnectFunc callback,
	               gpointer user_data,
	               GDestroyNotify destroy_data)
{
  return z_connector_new(Z_CLASS(ZStreamConnector), session_id, SOCK_STREAM, local, remote, sock_flags, callback, user_data, destroy_data);
}

#ifdef __cplusplus
}
#endif
#endif
