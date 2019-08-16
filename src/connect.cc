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

#include <zorpll/connect.h>
#include <zorpll/io.h>
#include <zorpll/log.h>
#include <zorpll/socketsource.h>
#include <zorpll/socket.h>
#include <zorpll/error.h>
#include <zorpll/streamfd.h>

#include <sys/types.h>
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <fcntl.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#include <io.h>
/* NOTE: on windows handles opened with socket() must call closesocket()
         and must NOT call any other close that triggers undefined behavior... */
#define close closesocket
#else
#  include <netinet/tcp.h>
#  include <netinet/in.h>
#  include <sys/poll.h>
#endif

/** 
 * Private callback function, registered to a #ZSocketSource to be called
 * when the socket becomes writeable, e.g.\ when the connection is
 * established.
 *
 * @param[in] timed_out specifies whether the operation timed out
 * @param[in] data user data passed by socket source, assumed to point to #ZConnector instance
 *
 * @returns always FALSE to indicate that polling the socket should end
 **/
static gboolean 
z_connector_connected(gboolean timed_out, gpointer data)
{
  ZConnector *self = (ZConnector *) data;
  int error_num = 0;
  const gchar * error_num_str = NULL;
  socklen_t errorlen = sizeof(error_num);
  ZConnectFunc callback;
  GError *err = NULL;
  gint fd;
  
  z_enter();
  if (!self->callback)
    z_return(FALSE); /* we have been called */
  
  fd = self->fd;
  if (timed_out)
    {
      error_num = ETIMEDOUT;
      error_num_str = "connection timed out";
    }
  else if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)(&error_num), &errorlen) == -1)
    {
      /*LOG
        This message indicates that getsockopt(SOL_SOCKET, SO_ERROR)
        failed for the given fd. This system call should never fail,
        so if you see this please report it to the BalaSys Development Team
	(at devel@balasys.hu).
       */
      z_log(self->session_id, CORE_ERROR, 0, "getsockopt(SOL_SOCKET, SO_ERROR) failed for connecting socket, ignoring; fd='%d', error='%s'", self->fd, g_strerror(errno));
    }
  if (error_num)
    {
      char buf1[MAX_SOCKADDR_STRING], buf2[MAX_SOCKADDR_STRING];
      if (!error_num_str)
        error_num_str = g_strerror(error_num);

      
      /*LOG
        This message indicates that the connection to the remote end
	 failed for the given reason. It is likely that the remote end
	 is unreachable.
       */
      z_log(self->session_id, CORE_ERROR, 2, "Connection to remote end failed; local='%s', remote='%s', error='%s'", 
               self->local ? z_sockaddr_format(self->local, buf1, sizeof(buf1)) : "NULL", 
               z_sockaddr_format(self->remote, buf2, sizeof(buf2)), error_num_str);
      
      /* self->poll.fd is closed when we are freed */
      fd = -1;
    }
  else
    {
#ifdef G_OS_WIN32
      WSAEventSelect(fd, 0, 0);
#endif
      z_fd_set_nonblock(fd, 0);
      
      /* don't close our fd when freed */
      self->fd = -1;
    }
  
  g_rec_mutex_lock(&self->lock);
  
  if (self->watch)
    {
      if (error_num)
        g_set_error(&err, G_IO_CHANNEL_ERROR, error_num, "%s", error_num_str);
      
      callback = self->callback;
      self->callback = NULL;
      callback(fd >= 0 ? z_stream_fd_new(fd, "") : NULL, err, self->user_data);
      
      g_clear_error(&err);
    }
  else
    {
      /*LOG
        This message reports that the connection was cancelled, and
        no further action is taken.
       */
      z_log(self->session_id, CORE_DEBUG, 6, "Connection cancelled, not calling callback; fd='%d'", fd);
      close(fd);
    }
  g_rec_mutex_unlock(&self->lock);

  /* don't poll again, and destroy associated source */
  z_return(FALSE);
}

/**
 * This function is registered as the destroy notify function of self when
 * the associated #ZSocketSource source is destroyed.
 *
 * @param[in] self ZConnector instance
 *
 * It calls our destroy_notify callback, and unrefs self.
 **/
static void
z_connector_source_destroy_cb(ZConnector *self)
{
  if (self->destroy_data && self->user_data)
    {
      self->destroy_data(self->user_data);
      self->user_data = NULL;
    }
  z_connector_unref(self);
}

/**
 * This function is used by the different z_connector_start_*() functions,
 * it contains the common things to do when a connection is initiated.
 *
 * @param[in]  self ZConnector instance
 * @param[out] local_addr if not NULL, the local address where we are bound will be returned here
 *
 * @returns TRUE if the connection succeeded.
 **/
static gboolean
z_connector_start_internal(ZConnector *self, ZSockAddr **local_addr)
{
  ZSockAddr *local = NULL;
  gchar buf1[MAX_SOCKADDR_STRING], buf2[MAX_SOCKADDR_STRING];

  z_enter();
  /*LOG
    This message reports that a new connection is initiated
    from/to the given addresses.
   */
  z_log(self->session_id, CORE_DEBUG, 7, "Initiating connection; from='%s', to='%s'",
        self->local ? z_sockaddr_format(self->local, buf1, sizeof(buf1)) : "NULL",
        z_sockaddr_format(self->remote, buf2, sizeof(buf2)));

  if (z_connect(self->fd, self->remote, self->sock_flags) != G_IO_STATUS_NORMAL && !z_errno_is(EINPROGRESS))
    {
      /*LOG
        This message indicates that the connection to the remote end
        failed for the given reason. It is likely that the remote end
        is unreachable.
       */
      z_log(self->session_id, CORE_ERROR, 2, "Connection to remote end failed; local='%s', remote='%s', error='%s'", 
            self->local ? z_sockaddr_format(self->local, buf1, sizeof(buf1)) : "NULL", 
            z_sockaddr_format(self->remote, buf2, sizeof(buf2)), g_strerror(errno));
      z_return(FALSE);
    }

  if (z_getsockname(self->fd, &local, self->sock_flags) == G_IO_STATUS_NORMAL)
    {
      ZSockAddr *l;
      
      /* it contained the bind address, we now have an exact value */
      l = self->local;
      self->local = NULL;
      z_sockaddr_unref(l);
      self->local = local;
      z_sockaddr_ref(local);
    }
  if (local_addr)
    *local_addr = local;
  else
    z_sockaddr_unref(local);
  return TRUE;
}

/**
 * Start initiating the connection.
 *
 * @param[in]  self ZConnector instance
 * @param[out] local_addr if not NULL, the local address where we are bound will be returned here
 *
 * It will fail if this function has been called on this ZConnector already.
 *
 * @returns TRUE if the connection was successful
 **/
gboolean
z_connector_start(ZConnector *self, ZSockAddr **local_addr)
{  
  z_enter();
  if (self->watch)
    {
      /*LOG
        This message indicates that the connection was started twice.
        Please report this error to the BalaSys Development Team (devel@balasys.hu).
       */
      z_log(self->session_id, CORE_ERROR, 3, "Internal error, z_connector_start was called twice;");
      z_return(FALSE);
    }

  if (z_connector_start_internal(self, local_addr))
    {
      self->watch = z_socket_source_new(self->fd, static_cast<GIOCondition>(Z_SOCKEVENT_CONNECT), self->timeout);
      
      g_source_set_callback(self->watch, (GSourceFunc) z_connector_connected, z_connector_ref(self), (GDestroyNotify) z_connector_source_destroy_cb);
      if (!g_source_attach(self->watch, self->context))
        g_assert_not_reached();
      z_leave();
      return TRUE;
    }
  
  z_return(FALSE);
}

/**
 * Initiate the connection and block while it either succeeds or fails.
 *
 * @param[in]  self ZConnector instance
 * @param[out] local_addr if not NULL, the local address where we are bound will be returned here
 * @param[out] stream if not NULL, a stream wrapped around self->fd will be returned here
 *
 * @returns TRUE to indicate success and a reference of the local address in local_addr
 **/
 
gboolean
z_connector_start_block(ZConnector *self, ZSockAddr **local_addr, ZStream **stream)
{  
  gint res;
  gboolean success = FALSE;

  z_enter();
  if (z_connector_start_internal(self, local_addr))
    {
      z_connector_ref(self);

#ifndef G_OS_WIN32
      {
        struct pollfd pfd;
        time_t timeout_target, timeout_left;

        pfd.fd = self->fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        timeout_target = time(NULL) + self->timeout;
        do
          {
            timeout_left = timeout_target - time(NULL);
            res = poll((struct pollfd *) &pfd, 1, timeout_left < 0 ? 0 : timeout_left * 1000);
            if (res == 1)
              break;
          }
        while (res == -1 && errno == EINTR);
      }
#else
      {
        fd_set rf;
        TIMEVAL to;
   
        to.tv_sec = 0;
        to.tv_usec = self->timeout * 1000;
        FD_ZERO(&rf);
        FD_SET(self->fd, &rf);

        do
          {
            res = select(0,&rf,&rf,&rf,&to); //   ((struct pollfd *) &pfd, 1, self->timeout);
            if (res == 1)
              break;
          }
        while (res == -1 && errno == EINTR);
      }
#endif
      z_fd_set_nonblock(self->fd, 0);
      z_fd_set_keepalive(self->fd, 1);
      success = TRUE;
      *stream = z_stream_fd_new(self->fd, "");
      z_connector_source_destroy_cb(self);
      self->fd = -1;
    }
  z_leave();
  return success;
}

/**
 * Same as z_connector_start() but using the context specified in context.
 *
 * @param[in]  self ZConnector instance
 * @param[in]  context GMainContext to use for polling
 * @param[out] local_addr if not NULL, the the local address where we are bound will be returned here
 *
 * @returns TRUE if successful.
 **/
gboolean
z_connector_start_in_context(ZConnector *self, GMainContext *context, ZSockAddr **local_addr)
{
  gboolean success;

  z_enter();
  if (context)
    {
      g_main_context_ref(context);
      self->context = context;
    }
  success = z_connector_start(self, local_addr);
  z_leave();  
  
  return success;
}

/**
 * Cancel connection after _start was called.
 *
 * @param[in] self ZConnector instance
 *
 * It is guaranteed that no user callbacks will be called after
 * z_connector_cancel() returns.
 **/
void
z_connector_cancel(ZConnector *self)
{
  z_enter();
  
  g_rec_mutex_lock(&self->lock);
  if(self->watch)
    {
      /* Must unlock self->lock before call g_source_destroy,
       * because in another thread we may be hold context lock
       * (inside the glib) and wait for this lock. (For example if
       * the client stop the download in exactly the same time, when
       * the connection failed.
       */
      GSource *watch = self->watch;
      self->watch = NULL;
      g_rec_mutex_unlock(&self->lock);

      g_source_destroy(watch);
      g_source_unref(watch);
    }
  else
    {
      g_rec_mutex_unlock(&self->lock);
    }
  z_return();
}

/**
 * Set the connection timeout of a #ZConnector.
 *
 * @param[in] self ZConnector instance
 * @param[in] timeout timeout in seconds
 *
 * Set connection timeout. The connection establishment may not exceed the
 * time specified in timeout.
 **/
void
z_connector_set_timeout(ZConnector *self, gint timeout)
{
  self->timeout = timeout;
}

void
z_connector_set_tos(ZConnector *self, gint tos)
{
  if ((self->fd != -1) && tos > 0)
    z_fd_set_our_tos(self->fd, tos);
}

void
z_connector_set_mark(ZConnector *self, int mark)
{
  if ((self->fd != -1) && mark > 0)
    z_fd_set_our_mark(self->fd, mark);
}

/**
 * This function opens a new socket and returns the newly created fd.
 *
 * @param[in] self ZConnector instance
 *
 * @returns the newly created fd
 **/
static gint
z_connector_open_socket(ZConnector *self)
{
  gint fd;
  gint on = 1;
  gchar buf[MAX_SOCKADDR_STRING];
  
  fd = socket(z_map_pf(self->remote->sa.sa_family), self->socket_type, 0);
  
  if (fd == -1)
    {
      
      /*LOG
        This message indicates that Zorp failed to create a socket for
        establishing a connection with the indicated remote endpoint.
       */
      z_log(self->session_id, CORE_ERROR, 1, "Creating socket for connecting failed; family='%d', type='%s', remote='%s', error='%s'", 
            self->remote->sa.sa_family, z_socket_type_to_str(self->socket_type), z_sockaddr_format(self->remote, buf, sizeof(buf)), g_strerror(errno));
      z_return(-1);
    }
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(const_cast<const gint *>(&on)), sizeof(on)) < 0)
    {
      z_log(self->session_id, CORE_ERROR, 1, "Enabling SO_REUSEADDR on connect socket failed; family='%d', type='%s', error='%s'", 
            self->remote->sa.sa_family, z_socket_type_to_str(self->socket_type), g_strerror(errno));
    }
  if (self->local && z_bind(fd, self->local, self->sock_flags) != G_IO_STATUS_NORMAL)
    {
      
      z_log(self->session_id, CORE_ERROR, 1, "Error binding socket; local='%s', error='%s'", z_sockaddr_format(self->local, buf, sizeof(buf)), g_strerror(errno));
      z_leave();
      return -1;
    }

  if (!z_fd_set_nonblock(fd, TRUE))
    {
      /* z_fd_set_nonblock sends log message for failure */
      z_leave();
      return -1;
    }
  z_leave();
  return fd;
}

/**
 * This function creates a new ZConnector instance.
 *
 * @param[in]      class the class of the new object, must be compatible with #ZConnector.
 * @param[in]      session_id session id used for logging
 * @param[in]      socket_type socket type
 * @param[in]      local local address to bind to.
 * @param[in]      remote remote address to connect to.
 * @param[in]      sock_flags socket flags
 * @param[in]      callback function to call when the connection is established.
 * @param[in]      user_data opaque pointer to pass to callback.
 * @param[in]      destroy_data destroy callback for user_data
 *
 * @returns The allocated instance.
 **/
ZConnector *
z_connector_new(ZClass *class_,
                const gchar *session_id,
                gint socket_type,
                ZSockAddr *local, 
                ZSockAddr *remote,
                guint32 sock_flags,
		ZConnectFunc callback,
		gpointer user_data,
		GDestroyNotify destroy_data)
{
  ZConnector *self;
  
  z_enter();
  self = Z_NEW_COMPAT(class_, ZConnector);
  self->refcnt = 1;
  self->local = z_sockaddr_ref(local);
  self->remote = z_sockaddr_ref(remote);
  self->session_id = session_id ? g_strdup(session_id) : NULL;
  self->callback = callback;
  self->user_data = user_data;
  self->destroy_data = destroy_data;
  self->timeout = 30;
  self->sock_flags = sock_flags;
  self->socket_type = socket_type;
  self->fd = z_connector_open_socket(self);
  g_rec_mutex_init(&self->lock);
  if (self->fd < 0)
    {
      z_connector_unref(self);
      self = NULL;
    }
  z_return((ZConnector *) self);
}

/**
 * Free the contents of s.
 *
 * @param[in] s ZConnector instance to free
 *
 * This function is called by z_connector_unref() when the reference count
 * of s goes down to zero. It frees the contents of s.
 **/
static void 
z_connector_free(ZObject *s)
{
  ZConnector *self = Z_CAST(s, ZConnector);
  z_enter();
  self->callback = NULL;
  g_rec_mutex_clear(&self->lock);

  if (self->destroy_data && self->user_data)
    {
      self->destroy_data(self->user_data);
      self->user_data = NULL;
    }
  if (self->fd != -1)
    close(self->fd);
  if (self->watch)
    {
      /* self->watch might still be present when the destruction of this
       * object is done by the FALSE return value of our callback.
       * 1) connected returns FALSE
       * 2) GSource calls our destroy notify, which drops the reference 
       *    held by the source
       * 3) when our ref_cnt goes to 0, this function is called, but 
       *    self->watch might still be present 
       *
       * Otherwise the circular reference is broken by _cancel or right in 
       * _start when the error occurs.
       */
      g_source_destroy(self->watch);
      g_source_unref(self->watch);
      self->watch = NULL;
    } 
  z_sockaddr_unref(self->local);
  z_sockaddr_unref(self->remote);
  if (self->context)
    g_main_context_unref(self->context);
  
  g_free(self->session_id);
  z_object_free_method(s);
  z_return();
}

/**
 * ZConnector virtual methods.
 **/
ZConnectorFuncs z_connector_funcs = 
{
  {
    Z_FUNCS_COUNT(ZObject),
    z_connector_free
  },
};

/**
 * ZConnector class descriptor.
 **/
#ifdef G_OS_WIN32
  LIBZORPLL_EXTERN
#endif 
Z_CLASS_DEF(ZConnector, ZObject, z_connector_funcs);

/**
 * ZStreamConnector virtual methods.
 **/
ZConnectorFuncs z_stream_connector_funcs = 
{
  {
    Z_FUNCS_COUNT(ZObject),
    NULL
  }
};

/**
 * ZStreamConnector class descriptor.
 **/
#ifdef G_OS_WIN32
  LIBZORPLL_EXTERN
#endif 
ZClass ZStreamConnector__class = 
{
  Z_CLASS_HEADER,
  Z_CLASS(ZConnector),            // super_class 
  "ZStreamConnector",             // name 
  sizeof(ZConnector),             // size 
  &z_stream_connector_funcs.super //funcs 
};
