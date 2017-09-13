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

#include <zorpll/listen.h>
#include <zorpll/io.h>
#include <zorpll/log.h>
#include <zorpll/socketsource.h>
#include <zorpll/streamfd.h>

#include <sys/types.h>
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <fcntl.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#  include <io.h>
#else
#  include <netinet/tcp.h>
#  include <netinet/in.h>
#endif

#include <zorpll/misc.h>

#define MAX_ACCEPTS_AT_A_TIME 50


/**
 * Private callback used as the callback of #ZSocketSource which
 * calls us when the listening socket becomes readable.
 *
 * @param[in] timed_out specifies whether the operation was timed out (unused)
 * @param[in] data user pointer pointing to self
 *
 * This function accepts the connection using z_accept() and calls the callback supplied
 * by our user.
 *
 * @todo FIXME: timed_out is unused. Why?
 *
 * @returns whether further callbacks should be delivered
 **/
static gboolean 
z_listener_accept(gboolean timed_out G_GNUC_UNUSED, gpointer data)
{
  ZListener *self = (ZListener *) data;
  ZSockAddr *client, *dest;
  gboolean rc = TRUE;
  ZStream *newstream;
  gint accepts = 0;
  GIOStatus res;
  time_t start;

  z_enter();
  /* NOTE: self->lock protects cancellation, _cancel grabs self->lock thus
   * we either execute first and accept all fds, or self->watch will be
   * NULL and we return */
  g_rec_mutex_lock(&self->lock);
  if (!self->watch)
    {
      g_rec_mutex_unlock(&self->lock);
      z_return(TRUE);
    }
    
  z_listener_ref((ZListener *) self);
  start=time(NULL);
  while (!z_socket_source_is_suspended(self->watch) && rc && accepts < MAX_ACCEPTS_AT_A_TIME && start == time(NULL))
    {  
      res = Z_FUNCS(self, ZListener)->accept_connection(self, &newstream, &client, &dest);
      if (res == G_IO_STATUS_NORMAL)
        {
#ifdef G_OS_WIN32
//FIXMEEEEEEEEEEEE
//          WSAEventSelect(newfd, 0, 0);
#endif
          z_stream_set_nonblock(newstream, 0);
        }
      else if (res == G_IO_STATUS_AGAIN)
        {
          break;
        }
      else
        {
          newstream = NULL;
          client = NULL;
        }
      
      rc = self->callback(newstream, client, dest, self->user_data);
      accepts++;
      if (self->sock_flags & ZSF_ACCEPT_ONE)
        rc = FALSE;
      if (!self->watch)
        break;
    }
  z_listener_unref((ZListener *) self);
  g_rec_mutex_unlock(&self->lock);
  
  /*LOG
    This message reports the number of accepted connections
    in one poll cycle. If this value is continually high then it
    is likely that the computer can not handle the incoming
    connection rate.
   */
  z_log(self->session_id, CORE_DEBUG, 7, "Accept count; accepts='%d'", accepts);
  z_return(rc);
}

/**
 * Open the listener through calling the open_listener virtual
 * function and store the file descriptor in the ZListener structure.
 *
 * @param[in] self ZListener instance
 *
 * @returns the new fd if successful, -1 if failed
 **/
gboolean
z_listener_open(ZListener *self)
{
  gint fd;
  gboolean res;

  z_enter();
  g_assert(Z_FUNCS(self, ZListener)->open_listener != NULL);
  fd = Z_FUNCS(self, ZListener)->open_listener(self);
  if (fd == -1)
    res = FALSE;
  else
    {
      self->fd = fd;
      res = TRUE;
    }
  z_return(res);
}

/**
 * Start polling to the listening socket in the main context.
 *
 * @param[in] s ZListener instance
 *
 * @returns TRUE if successful, FALSE if it isn't open yet or the connection was started already.
 **/
gboolean
z_listener_start(ZListener *s)
{
  ZListener *self = (ZListener *) s;
  gchar buf[MAX_SOCKADDR_STRING];
  
  z_enter();
  if (self->watch)
    {
      /*LOG
        This message indicates that the connection was started twice and
        this second attempt is ignored.
       */
      z_log(self->session_id, CORE_ERROR, 4, "Internal error z_listener_start called twice, ignoring;");
      z_return(FALSE);
    }

  if (self->fd == -1)
    {
      /* the open callback has not been called yet, so we don't yet have an fd */
      if (!z_listener_open(self))
        z_return(FALSE);
    }

  /*LOG
    This message reports that listening on the given address is
    successfully started.
   */
  z_log(self->session_id, CORE_DEBUG, 7, "Start to listen; fd='%d', address='%s'", self->fd, z_sockaddr_format(self->local, buf, sizeof(buf)));

  /* our callback might be called immediately, which in turn may free us,
     thus the incremented reference here. */
  z_listener_ref(s);
  self->watch = z_socket_source_new(self->fd, static_cast<GIOCondition>(Z_SOCKEVENT_ACCEPT), -1);
  g_source_set_callback(self->watch, (GSourceFunc) z_listener_accept, self, (GDestroyNotify) z_listener_unref);
  g_source_attach(self->watch, self->context);
  z_return(TRUE);
}

/**
 * Start listening to the socket in the specified context.
 *
 * @param[in] s ZListener instance
 * @param[in] context GMainContext to use for polling
 *
 * @returns TRUE on success
 **/
gboolean
z_listener_start_in_context(ZListener *s, GMainContext *context)
{
  gboolean res;
  ZListener *self = (ZListener *) s;
  
  z_enter();
  g_main_context_ref(context);
  self->context = context;
  res = z_listener_start(s);
  z_return(res);
}

/**
 * Temporarily suspend listening on the socket.
 *
 * @param[in] s ZListener instance
 *
 * Further callbacks will not be delivered until z_listener_resume() is called.
 **/
void
z_listener_suspend(ZListener *s)
{
  ZListener *self = (ZListener *) s;
  
  z_enter();
  if (self->watch)
    z_socket_source_suspend(self->watch);
  z_return();
}

/**
 * Resume a suspended listener.
 *
 * @param[in] self ZListener instance
 **/
void
z_listener_resume(ZListener *self)
{
  z_enter();
  if (self->watch)
    z_socket_source_resume(self->watch);
  z_return();
}

/**
 * Cancel listening.
 *
 * @param[in] self ZListener instance
 *
 * No user callbacks will be called after returning from
 * z_listener_cancel().
 **/
void
z_listener_cancel(ZListener *self)
{
  z_enter();
  if (self->watch)
    {
      GSource *watch;
      
      /* NOTE: this locks out our accepted callback. We either accept all
       * pending fds, or we NULL out self->watch and our accepted callback
       * won't call any user callbacks. It is therefore guaranteed that no
       * user callbacks will be called after cancellation */
      g_rec_mutex_lock(&self->lock);
      watch = self->watch;
      self->watch = NULL;
      g_rec_mutex_unlock(&self->lock);

      g_source_destroy(watch);
      g_source_unref(watch);
    }
  z_return();
}

/**
 * This function creates a new ZListener instance.
 *
 * @param[in]      class the class of the new object, must be compatible with #ZListener.
 * @param[in]      session_id session id
 * @param[in]      bind_addr address to bind to.
 * @param[in]      sock_flags a combination of socket flags (ZSF_*)
 * @param[in]      callback function to call, when an incoming connection is accepted.
 * @param[in]      user_data opaque pointer passed to callback.
 *
 * Listening to the socket will not be started until z_listener_start() is called.
 *
 * @returns the new ZListener instance
 **/
ZListener *
z_listener_new(ZClass *klass,
               const gchar *session_id,
               ZSockAddr *bind_addr, 
               guint32 sock_flags,
               ZAcceptFunc callback,
	       gpointer user_data)
{
  ZListener *self;

  z_enter();
  self = Z_NEW_COMPAT(klass, ZListener);
  self->session_id = session_id ? g_strdup(session_id) : NULL;
  self->callback = callback;
  self->user_data = user_data;
  self->sock_flags = sock_flags;
  self->bind_addr = z_sockaddr_ref(bind_addr);
  self->fd = -1;
  g_rec_mutex_init(&self->lock);
  z_return(self);
}


/**
 * A private function called when the reference count of the ZListener
 * instance goes down to zero.
 *
 * @param[in] s ZListener instance
 *
 * It frees all instance variables and the structure itself.
 **/
static void 
z_listener_free(ZObject *s)
{
  ZListener *self = Z_CAST(s, ZListener);

  z_enter();
  self->callback = NULL;
  g_rec_mutex_clear(&self->lock);
  if (self->fd != -1)
    {
#ifdef G_OS_WIN32
      closesocket(self->fd);
#else
      close(self->fd);
#endif
    }
  if (self->context)
    g_main_context_unref(self->context);
  z_sockaddr_unref(self->bind_addr);
  z_sockaddr_unref(self->local);
  g_free(self->session_id);
  z_object_free_method(s);
  z_return();
}

/**
 * ZListener virtual methods.
 **/
ZListenerFuncs z_listener_funcs = 
{
  {
    Z_FUNCS_COUNT(ZListener),
    z_listener_free,
  },
  NULL,
  NULL
};

/**
 * ZListener class descriptor.
 **/
Z_CLASS_DEF(ZListener, ZObject, z_listener_funcs);

/**
 * Stream listener.
 **/
typedef struct _ZStreamListener 
{
  ZListener super;
  gint backlog;
} ZStreamListener;

extern ZClass ZStreamListener__class;

/**
 * Create the listener socket.
 *
 * @param[in] s ZStreamListener instance
 *
 * @returns the new fd if successful, -1 if failed
 **/
static gint
z_stream_listener_open_listener(ZListener *s)
{
  ZStreamListener *self = Z_CAST(s, ZStreamListener);
  gint fd;
  
  z_enter();
  fd = socket(z_map_pf(s->bind_addr->sa.sa_family), SOCK_STREAM, 0);
  if (fd == -1)
    {
      /*LOG
        This message indicate that the creation of a new socket failed
        for the given reason. It is likely that the system is running low
        on memory, or the system is running out of the available fds.
       */
      z_log(s->session_id, CORE_ERROR, 2, "Cannot create socket; error='%s'", g_strerror(errno));
      z_return(-1);
    }
  z_fd_set_nonblock(fd, 1);
  if ((s->bind_addr && z_bind(fd, s->bind_addr, s->sock_flags) != G_IO_STATUS_NORMAL) ||  
      (z_listen(fd, self->backlog, s->sock_flags) != G_IO_STATUS_NORMAL) ||
      (z_getsockname(fd, &s->local, s->sock_flags) != G_IO_STATUS_NORMAL))
    { 
      close(fd);
      z_return(-1);
    }
  z_return(fd);
}

static GIOStatus
z_stream_listener_accept_connection(ZListener *self, ZStream **fdstream, ZSockAddr **client, ZSockAddr **dest)
{
  gint newfd;
  GIOStatus res;
  
  res = z_accept(self->fd, &newfd, client, self->sock_flags);
  
  if (res != G_IO_STATUS_NORMAL)
    {
      return res;
    }
  *fdstream = z_stream_fd_new(newfd, "");
  *dest = NULL;
  z_getdestname(newfd, dest, self->sock_flags);
  return res;
}

ZListener *
z_stream_listener_new(const gchar *session_id,
                   ZSockAddr *local,
                   guint32 sock_flags,
	           gint backlog,
                   ZAcceptFunc callback,
                   gpointer user_data)
{
  ZStreamListener *self;
  
  self = Z_CAST(z_listener_new(Z_CLASS(ZStreamListener), session_id, local, sock_flags, callback, user_data), ZStreamListener);
  if (self)
    {
      self->backlog = backlog;
    }
  return &self->super;
}

/**
 * ZListener virtual methods.
 **/
ZListenerFuncs z_stream_listener_funcs = 
{
  {
    Z_FUNCS_COUNT(ZListener),
    z_listener_free,
  },
  z_stream_listener_open_listener,
  z_stream_listener_accept_connection
};

/**
 * ZListener class descriptor.
 **/
Z_CLASS_DEF(ZStreamListener, ZListener, z_stream_listener_funcs);
