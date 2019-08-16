/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/streamfd.h>

#include <zorpll/log.h>
#include <zorpll/error.h>

#include <string.h>
#include <sys/types.h>
#include <assert.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#  include <zorpll/io.h>
#else
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <sys/poll.h>
#endif

/**
 * ZStream-derived class that can be used to read/write an fd.
 *
 * On Windows, this can only be a WinSock fd.
 **/
typedef struct _ZStreamFD
{
  ZStream super;

  GIOChannel *channel;
  gint fd;
  gint keepalive;
  gboolean shutdown;
  GPollFD pollfd;
#ifdef G_OS_WIN32
  int winsock_event;
  gboolean can_write;
#endif
} ZStreamFD;

extern ZClass ZStreamFD__class;

/**
 * ZStreamFD extra context data.
 **/
typedef struct _ZStreamFDExtra
{
  gboolean nonblock;
} ZStreamFDExtra;

static gboolean
z_stream_fd_watch_prepare(ZStream *s, GSource *src G_GNUC_UNUSED, gint *timeout)
{
  ZStreamFD *mystream = (ZStreamFD *) s;
  GPollFD *mypollfd = &mystream->pollfd;

#ifdef G_OS_WIN32
  fd_set xf,wf;
  struct timeval to;
  int res;
#endif

  z_enter();

  *timeout = -1;

  if (mypollfd->revents)
    {
      *timeout = 0;
      z_return(TRUE);
    }

#ifdef G_OS_WIN32
  if (mystream->super.want_write)
    {
      FD_ZERO(&xf);
      FD_ZERO(&wf);
      FD_SET(mystream->fd,&wf);
      to.tv_sec = to.tv_usec = 0;
      res = select(0,&xf,&wf,&xf,&to);
      if (res == 1)
        {
          *timeout = 0;
          mystream->can_write = TRUE;
          z_trace(NULL, "WinSock: select said fd %d is writable at %s line %d", mystream->fd, __FILE__, __LINE__);
        }
    }
  else
    {
      mystream->can_write = FALSE;
    }

  mypollfd->events = FD_CLOSE;

  if (mystream->super.want_read)
    mypollfd->events |= FD_READ;

  if ((mystream->super.want_write) && !(mystream->can_write))
    mypollfd->events |= FD_WRITE | FD_CONNECT; // MS KnowledgeBase article #Q199434

  if (mystream->super.want_pri)
    mypollfd->events |= FD_OOB;

  z_trace(NULL, "WinSock: WSAEventSelect(%d,%d,%o) at %s line %d", mystream->fd, mypollfd->fd, mypollfd->events, __FILE__, __LINE__);
  if (WSAEventSelect(mystream->fd, reinterpret_cast<HANDLE>(mypollfd->fd), mypollfd->events) == SOCKET_ERROR) //(HANDLE)
    {
      /*LOG
        This message indicates an internal error during WSAEventSelect. Please report this event
	to the BalaSys Development Team (devel@balasys.hu).
       */
      z_log(NULL, CORE_ERROR, 1, "Failed to set up WinSock event for z_stream_fd; error_code='%d'", z_errno_get());
      z_return(FALSE);
    }
  mypollfd->events = G_IO_IN;

#else

  mypollfd->events = 0;

  if (mystream->super.want_read)
    mypollfd->events |= G_IO_IN;

  if (mystream->super.want_write)
    mypollfd->events |= G_IO_OUT;

  if (mystream->super.want_pri)
    mypollfd->events |= G_IO_PRI;

  if (mypollfd->events != 0 && mystream->shutdown)
    {
      *timeout = 0;
      z_return(TRUE);
    }

  if (mypollfd->events == 0)
    mypollfd->events = G_IO_ERR;

#endif
  z_return(FALSE);
}

static gboolean
z_stream_fd_watch_check(ZStream *s, GSource *src G_GNUC_UNUSED)
{
  ZStreamFD *mystream = (ZStreamFD *) s;
  GPollFD *mypollfd = &mystream->pollfd;
  GIOCondition poll_condition;

#ifdef G_OS_WIN32
  WSANETWORKEVENTS evs;
#endif

  z_enter();

  if ((mypollfd->events & ~G_IO_ERR) != 0 && mystream->shutdown)
    z_return(TRUE);

#ifdef G_OS_WIN32
  WSAEnumNetworkEvents(mystream->fd, reinterpret_cast<HANDLE>(mypollfd->fd), &evs); //(HANDLE)
  mypollfd->revents = 0;

  if (mystream->can_write)
    mypollfd->revents |= G_IO_OUT;
  if (evs.lNetworkEvents & FD_WRITE)
    {
      if (evs.iErrorCode[FD_WRITE_BIT] != 0)
        mypollfd->revents |= G_IO_ERR;
      else
        mypollfd->revents |= G_IO_OUT;
    }
  if (evs.lNetworkEvents & FD_READ)
    {
      if (evs.iErrorCode[FD_READ_BIT] != 0)
        mypollfd->revents |= G_IO_ERR;
      else
        mypollfd->revents |= G_IO_IN;
    }
  if (evs.lNetworkEvents & FD_OOB)
    {
      if (evs.iErrorCode[FD_OOB_BIT] != 0)
        mypollfd->revents |= G_IO_ERR;
      else
        mypollfd->revents |= G_IO_PRI;
    }
  if (evs.lNetworkEvents & FD_CLOSE)
    mypollfd->revents |= G_IO_HUP; //v maybe G_IO_IN as well?
  z_trace(NULL, "WinSock: Event %o/%o on fd %d at %s line %d", evs.lNetworkEvents, mypollfd->revents, mypollfd->fd, __FILE__, __LINE__);
#endif

  poll_condition = static_cast<GIOCondition>(mypollfd->revents);
  z_return(poll_condition != 0);
}

static gboolean
z_stream_fd_watch_dispatch(ZStream *s, GSource *src)
{
  ZStreamFD *mystream = (ZStreamFD *) s;
  GPollFD *mypollfd = &mystream->pollfd;
  GIOCondition poll_cond = static_cast<GIOCondition> (mypollfd->revents);
  gboolean rc = TRUE;

  z_enter();
#ifdef G_OS_WIN32
  z_trace(NULL, "WinSock: Dispatching events %o for fd %d at %s line %d", mypollfd->revents, mypollfd->fd, __FILE__, __LINE__);
#endif

  mypollfd->revents = 0;

  if (mystream->shutdown ||
      (poll_cond & (G_IO_ERR | G_IO_HUP) && rc))
    {
      if (mystream->super.want_read)
        rc = (*mystream->super.read_cb)(&mystream->super, poll_cond, mystream->super.user_data_read);

      else if (mystream->super.want_write)
        rc = (*mystream->super.write_cb)(&mystream->super, poll_cond, mystream->super.user_data_write);

      else if (mystream->super.want_pri)
        rc = (*mystream->super.pri_cb)(&mystream->super, poll_cond, mystream->super.user_data_pri);

      else if (!mystream->shutdown)
        {
          /* basically an EOF */
          z_log(mystream->super.name, CORE_DEBUG, 6, "POLLERR or POLLHUP received, handling as EOF; poll_cond='%x'", poll_cond);
          mystream->shutdown = TRUE;
          g_source_remove_poll(src, mypollfd);
        }

      z_return(rc);
    }

  if (mystream->super.want_read && ((poll_cond & G_IO_IN) || mystream->shutdown) && rc)
    {
      if (mystream->super.read_cb)
        {
          rc = (*mystream->super.read_cb)(&mystream->super, poll_cond, mystream->super.user_data_read);
        }
      else
        {
          /*LOG
            This message indicates an internal error, read event occurred, but no read
            callback is set. Please report this event to the BalaSys Development Team (devel@balasys.hu).
           */
          z_log(mystream->super.name, CORE_ERROR, 3, "Internal error, no read callback is set;");
        }
    }

  if (mystream->super.want_write && (poll_cond & G_IO_OUT) && rc)
    {
      if (mystream->super.write_cb)
        {
          rc &= (*mystream->super.write_cb)(&mystream->super, poll_cond, mystream->super.user_data_write);
        }
      else
        {
	  /*LOG
	    This message indicates an internal error, write event occurred, but no write
	    callback is set. Please report this event to the BalaSys Development Team (devel@balasys.hu).
	   */
          z_log(mystream->super.name, CORE_ERROR, 3, "Internal error, no write callback is set;");
        }
    }

  if (mystream->super.want_pri && (poll_cond & G_IO_PRI) && rc)
    {
      if (mystream->super.pri_cb)
        {
          rc &= (*mystream->super.pri_cb)(&mystream->super, poll_cond, mystream->super.user_data_pri);
        }
      else
        {
	  /*LOG
	    This message indicates an internal error, pri-read event occurred, but no pri
	    callback is set. Please report this event to the BalaSys Development Team (devel@balasys.hu).
	   */
          z_log(mystream->super.name, CORE_ERROR, 3, "Internal error, no pri callback is set;");
        }
    }
  z_return(rc);
}

/**
 * Wait for the fd encapsulated by self.
 *
 * @param[in] self ZStreamFD instance
 * @param[in] cond events to wait for
 * @param[in] timeout timeout
 *
 * On win32 select is used. On other platforms poll is used.
 **/
#ifndef G_OS_WIN32

static gboolean
z_stream_wait_fd(ZStreamFD *self, guint cond, gint timeout)
{
  struct pollfd pfd;
  gint res;
  GIOFlags flags;

  z_enter();
  flags = g_io_channel_get_flags(self->channel);
  if ((flags & G_IO_FLAG_NONBLOCK) || timeout == -2)
    z_return(TRUE);
  errno = 0;
  pfd.fd = self->fd;
  pfd.events = cond;
  pfd.revents = 0;
  do
    {
      res = poll(&pfd, 1, timeout);
      if (res == 1)
        z_return(TRUE);
    }
  while (res == -1 && errno == EINTR);
  errno = ETIMEDOUT;
  z_return(FALSE);
}

#else

static gboolean
z_stream_wait_fd(ZStreamFD *self, guint cond, gint timeout)
{
  fd_set rf,wf;
  struct timeval to,*tout;
  int res;
  GIOFlags flags;

  z_enter();
  if (cond & G_IO_IN)
    {
      u_long bytes;

      ioctlsocket(self->fd, FIONREAD, &bytes);
      if (bytes > 0)
        z_return(TRUE);
    }

  FD_ZERO(&rf);
  if (cond & (G_IO_IN | G_IO_HUP))
    FD_SET(self->fd,&rf);

  FD_ZERO(&wf);
  if (cond & G_IO_OUT)
    FD_SET(self->fd,&wf);

  flags = g_io_channel_get_flags(self->channel);
  if ((flags & G_IO_FLAG_NONBLOCK) || timeout == -2)
    z_return(TRUE);

  if (timeout > 0)
    {
      to.tv_sec = timeout/1000;
      to.tv_usec = (timeout % 1000) * 1000;
      tout = &to;
    }
  else
    tout = NULL;

  res = select(0, &rf, &wf, NULL, tout);
  if ((res == 0) || (res == SOCKET_ERROR))
    {
      z_errno_set(ETIMEDOUT);
      z_return(FALSE);
    }
  z_return(TRUE);
}

#endif

/**
 * Read from the fd encapsulated by a ZStreamFD instance.
 *
 * @param[in]  stream ZStreamFD instance
 * @param[in]  buf buffer to read to
 * @param[in]  count number of bytes to ask for
 * @param[out] bytes_read number of bytes actually read will be put here
 * @param[out] error error value
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_fd_read_method(ZStream *stream,
		        void *buf,
		        gsize count,
		        gsize *bytes_read,
		        GError **error)
{
  ZStreamFD *self = (ZStreamFD *) stream;
  GIOStatus res;
  GError *local_error = NULL;

  z_enter();

  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if (!z_stream_wait_fd(self, G_IO_IN | G_IO_HUP, self->super.timeout))
    {
      g_set_error(&local_error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Channel read timed out");
      res = G_IO_STATUS_ERROR;
    }
  else
    {
      res = g_io_channel_read_chars(self->channel, static_cast<gchar *>(buf), count, bytes_read, &local_error);
    }
  if (!(self->super.umbrella_state & G_IO_IN))
    {
      /* low-level logging if we're not the toplevel stream */

      if (res == G_IO_STATUS_NORMAL)
        {
          /*LOG
            This message reports the number of bytes read from the given fd.
           */
          z_log(self->super.name, CORE_DUMP, 8, "Reading channel; fd='%d', count='%zd'", self->fd, *bytes_read);
          z_log_data_dump(self->super.name, CORE_DUMP, 10, buf, *bytes_read);
        }
      else if (res == G_IO_STATUS_EOF)
        {
          /*LOG
            This message reports that EOF was read from the given fd.
           */
          z_log(self->super.name, CORE_DUMP, 8, "Reading EOF on channel; fd='%d'", self->fd);
        }
    }

  if (local_error)
    g_propagate_error(error, local_error);
  z_return(res);
}

/**
 * Write to the fd encapsulated by a ZStreamFD instance.
 *
 * @param[in]  stream ZStreamFD instance
 * @param[in]  buf buffer to write the contents of
 * @param[in]  count number of bytes to write
 * @param[out] bytes_written actual number of bytes written will be returned here
 * @param[out] error error value
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_fd_write_method(ZStream *stream,
		         const void *buf,
		         gsize count,
		         gsize *bytes_written,
		         GError **error)
{
  ZStreamFD *self = (ZStreamFD *) stream;
  GIOStatus res;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if (self->shutdown)
    {
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Channel already shut down");
      z_return(G_IO_STATUS_ERROR);
    }

  if (!z_stream_wait_fd(self, G_IO_OUT | G_IO_HUP, self->super.timeout))
    {
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Channel write timed out");
      z_return(G_IO_STATUS_ERROR);
    }

  res = g_io_channel_write_chars(self->channel, static_cast<const gchar *>(buf), count, bytes_written, error);
  if (!(self->super.umbrella_state & G_IO_OUT))
    {
      /* low-level logging if we're not the toplevel stream */

      if (res != G_IO_STATUS_AGAIN)
        {
          /*LOG
            This message reports the number of bytes read from the given fd.
           */
          z_log(self->super.name, CORE_DUMP, 8, "Writing channel; fd='%d', count='%zd'", self->fd, *bytes_written);
          z_log_data_dump(self->super.name, CORE_DUMP, 10, buf, *bytes_written);
        }
    }
  z_return(res);
}

/**
 * Write priority data to the fd encapsulated by a ZStreamFD instance.
 *
 * @param[in]  stream ZStreamFD instance
 * @param[in]  buf buffer to write the contents of
 * @param[in]  count number of bytes to write
 * @param[out] bytes_written actual number of bytes written will be returned here
 * @param[out] error error value
 *
 * @returns GIOStatus value
 **/
GIOStatus
z_stream_fd_write_pri_method(ZStream *stream,
                             const void *buf,
                             gsize count,
                             gsize *bytes_written,
                             GError **error)
{
  ZStreamFD *self = (ZStreamFD *) stream;
  int res, attempt = 1;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if (self->shutdown)
    {
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Channel already shut down");
      z_return(G_IO_STATUS_ERROR);
    }

  do
    {
      if (!z_stream_wait_fd(self, G_IO_OUT | G_IO_HUP, self->super.timeout))
        {
          /*LOG
            This message indicates that send() timed out.
           */
          z_log(self->super.name, CORE_ERROR, 1, "Send timed out; fd='%d'", self->fd);
          g_set_error (error, G_IO_CHANNEL_ERROR,
                       G_IO_CHANNEL_ERROR_FAILED,
                       "Channel send timed out");
          z_return(G_IO_STATUS_ERROR);
        }
      res = send(self->fd, static_cast<const char *>(buf), count, MSG_OOB);
      if (res == -1)
        {
          if (!z_errno_is(EINTR) && !z_errno_is(EAGAIN))
            {
              /*LOG
                This message indicates that some I/O error occurred in the
                send() system call.
               */
              z_log(self->super.name, CORE_ERROR, 1, "Send failed; attempt='%d', error='%s'", attempt++, g_strerror(errno));
            }
        }
    }
  while (res == -1 && z_errno_is(EINTR));

  if (res >= 0)
    {
      *bytes_written = res;
      self->super.bytes_sent += res;
      z_return(G_IO_STATUS_NORMAL);
    }

  if (z_errno_is(EAGAIN))
    z_return(G_IO_STATUS_AGAIN);

  g_clear_error(error);
  g_set_error (error, G_IO_CHANNEL_ERROR,
               g_io_channel_error_from_errno (z_errno_get()),
               "%s",
               strerror (z_errno_get()));
  z_return(G_IO_STATUS_ERROR);
}

/**
 * Close the socket associated with a ZStreamFD instance.
 *
 * @param[in]  stream ZStreamFD instance
 * @param[in]  i HOW argument to shutdown
 * @param[out] error error value
 *
 * shutdown(2) will be called on the fd.
 *
 * The action to perform is specified by i as follows:
 * - i == 0: Stop receiving data.
 * - i == 1: Stop trying to transmit data.
 * - i == 2: Stop both reception and transmission.
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_fd_shutdown_method(ZStream *stream, int i, GError **error)
{
  ZStreamFD *self = (ZStreamFD *) stream;
  int res, attempt = 1;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  /*LOG
    This debug message indicates that shutdown is to be initiated on
    the given fd.
   */
  z_log(self->super.name, CORE_DEBUG, 6, "Shutdown channel; fd='%d', mode='%d'", self->fd, i);

  do
    {
      res = shutdown(self->fd, i);
      if (res == -1)
        {
          if (!z_errno_is(EINTR))
            {
              /*LOG
                This message indicates that the shutdown on the given fd was
                not successful.
               */
              z_log(self->super.name, CORE_ERROR, 4, "Shutdown failed; attempt='%d', error='%s'", attempt++, g_strerror(errno));
            }
        }
    }
  while (res == -1 && z_errno_is(EINTR));

  if (res != 0)
    {
      g_set_error (error, G_IO_CHANNEL_ERROR,
                   g_io_channel_error_from_errno (z_errno_get()),
                   "%s",
                   strerror (z_errno_get()));
      z_return(G_IO_STATUS_ERROR);
    }
  z_return(G_IO_STATUS_NORMAL);
}

/**
 * Close the fd associated with a ZStreamFD instance.
 *
 * @param[in]  s ZStreamFD instance
 * @param[out] error error value
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_fd_close_method(ZStream *s, GError **error)
{
  ZStreamFD *self = Z_CAST(s, ZStreamFD);
  GIOStatus res;

  z_enter();
  res = Z_SUPER(self, ZStream)->close(s, error);
  if (res == G_IO_STATUS_NORMAL)
    res = g_io_channel_shutdown(self->channel, TRUE, error);
  z_return(res);
}

/**
 * Process stream control calls.
 *
 * @param[in]      s ZStream instance
 * @param[in]      function function selector
 * @param[in, out] value parameter to function
 * @param[in]      vlen length of value
 *
 * @returns TRUE on success
 **/
static gboolean
z_stream_fd_ctrl_method(ZStream *s, guint function, gpointer value, guint vlen)
{
  ZStreamFD *self = Z_CAST(s, ZStreamFD);

  z_enter();
  switch (ZST_CTRL_MSG(function))
    {
    case ZST_CTRL_SET_CLOSEONEXEC:
      if (vlen == sizeof(gboolean))
        {
          gboolean cloexec = *((gboolean *)value);
          gint fd = self->fd;
          gint res;

#ifndef G_OS_WIN32
          if (cloexec)
            res = fcntl(fd, F_SETFD, FD_CLOEXEC);
          else
            res = fcntl(fd, F_SETFD, ~FD_CLOEXEC);
#else
          res = 0;
#endif
          if (res >= 0)
            z_return(TRUE);

          /*LOG
            This message indicates that an internal error occurred, during setting CLOSE_ON_EXEC mode
            on a stream. Please report this event to the BalaSys Development Team (devel@balasys.hu).
           */
          z_log(NULL, CORE_ERROR, 4, "Internal error, during setting CLOSE_ON_EXEC mode;");
        }
      else
        {
          /*LOG
            This message indicates that an internal error occurred, during setting CLOSE_ON_EXEC
            mode on a stream, because the size of the parameter is wrong. Please report this
            event to the BalaSys Development Team (devel@balasys.hu).
           */
          z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for setting CLOSE_ON_EXEC mode; size='%d'", vlen);
        }
      break;

    case ZST_CTRL_SET_NONBLOCK:
      if (vlen == sizeof(gboolean))
        {
          gboolean nonblock = *((gboolean *)value);
          GIOStatus ret;
          GIOFlags flags;

          flags = g_io_channel_get_flags(self->channel);
#ifndef G_OS_WIN32
          if (nonblock)
            ret = g_io_channel_set_flags(self->channel, static_cast<GIOFlags>(flags | G_IO_FLAG_NONBLOCK), NULL);
          else
            ret = g_io_channel_set_flags(self->channel, static_cast<GIOFlags>(flags & ~G_IO_FLAG_NONBLOCK), NULL);
#else
          z_fd_set_nonblock(self->fd, nonblock);
          ret = G_IO_STATUS_NORMAL;
#endif
          if (ret == G_IO_STATUS_NORMAL)
            z_return(TRUE);

          /*LOG
            This message indicates that an internal error, during setting NONBLOCK mode on
            a stream. Please report this event to the BalaSys Development Team (devel@balasys.hu).
           */
          z_log(NULL, CORE_ERROR, 4, "Internal error, during setting NONBLOCK mode;");
        }
      else
        /*LOG
          This message indicates that an internal error occurred, during setting NONBLOCK mode
          on a stream, because the size of the parameter is wrong. Please report this event to
          the BalaSys Development Team (devel@balasys.hu).
         */
        z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for setting NONBLOCK mode; size='%d'", vlen);
      break;

    case ZST_CTRL_GET_NONBLOCK:
      if (vlen == sizeof(gboolean))
        {
          GIOFlags flags;

          flags = g_io_channel_get_flags(self->channel);
          *((gboolean *) value) = !!(flags & G_IO_FLAG_NONBLOCK);
          z_return(TRUE);
        }
      /*LOG
        This message indicates that an internal error occurred, during getting NONBLOCK mode status
        on a stream, because the size of the parameter is wrong. Please report this event to
        the BalaSys Development Team (devel@balasys.hu).
       */
      z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for getting the NONBLOCK mode; size='%d'", vlen);
      break;

    case ZST_CTRL_GET_FD:
      if (vlen == sizeof(gint))
        {
          *((gint *)value) = self->fd;
          z_return(TRUE);
        }
      /*LOG
        This message indicates that an internal error occurred, during getting the FD of a stream,
        because the size of the parameter is wrong. Please report this event to the BalaSys Development
        Team (devel@balasys.hu).
       */
      z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for getting the FD; size='%d'", vlen);
      break;

    case ZST_CTRL_GET_BROKEN:
#ifndef G_OS_WIN32
      if (vlen == sizeof(gboolean))
        {
          gchar buf[1];
          gint res;

          res = recv(self->fd, buf, sizeof(buf), MSG_PEEK | MSG_DONTWAIT);
          if ((res < 0 && (z_errno_is(EAGAIN) || z_errno_is(ENOTSOCK))) || res > 0)
            *((gboolean *)value) = FALSE;
          else
            *((gboolean *)value) = TRUE;
          z_return(TRUE);
        }
      /*LOG
        This message indicates that an internal error occurred, during getting the FD of a stream,
        because the size of the parameter is wrong. Please report this event to the BalaSys Development
        Team (devel@balasys.hu).
       */
      z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for getting the broken state; size='%d'", vlen);
#else
      z_log(NULL, CORE_ERROR, 4, "Internal error, this feature is not supported on Win32;");
      z_return(FALSE);
#endif
      break;

    case ZST_CTRL_GET_KEEPALIVE:
      if (vlen == sizeof(gint))
        {
          *((gint *)value) = self->keepalive;
          z_leave();
          return TRUE;
        }
      else
        /*LOG
          This message indicates that an internal error occurred, during getting the FD of a stream,
          because the size of the parameter is wrong. Please report this event to the BalaSys Development
          Team (devel@balasys.hu).
          */
        z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for getting the KEEPALIVE option; size='%d'", vlen);
      break;

    case ZST_CTRL_SET_KEEPALIVE:
      if (vlen == sizeof(gint))
        {
          self->keepalive = *((gint *)value);
          z_leave();
          return TRUE;
        }
      else
        /*LOG
          This message indicates that an internal error occurred, during getting the FD of a stream,
          because the size of the parameter is wrong. Please report this event to the BalaSys Development
          Team (devel@balasys.hu).
          */
        z_log(NULL, CORE_ERROR, 4, "Internal error, bad parameter is given for setting the KEEPALIVE option; size='%d'", vlen);
      break;

    default:
      if (z_stream_ctrl_method(s, function, value, vlen))
        z_return(TRUE);
      /*LOG
        This message indicates that an internal error occurred, because an invalid control was called.
        Please report this event to the BalaSys Development Team (devel@balasys.hu).
       */
      z_log(NULL, CORE_ERROR, 4, "Internal error, unknown stream ctrl; ctrl='%d'", ZST_CTRL_MSG(function));
      break;
    }
  z_return(FALSE);
}

static void
z_stream_fd_attach_source_method(ZStream *s, GMainContext *context)
{
  ZStreamFD *self = (ZStreamFD *) s;
  z_enter();

  Z_SUPER(self, ZStream)->attach_source(s, context);
#ifdef G_OS_WIN32
  self->pollfd.fd = self->winsock_event;
  z_trace(NULL, "WinSock: Event #%d created at %s line %d", self->pollfd.fd, __FILE__, __LINE__);
#else
  self->pollfd.fd = self->fd;
#endif
  g_source_add_poll(s->source, &self->pollfd);
  z_return();
}

static gsize
z_stream_fd_extra_get_size_method(ZStream *s)
{
  return Z_SUPER(s, ZStream)->extra_get_size(s) + sizeof(ZStreamFDExtra);
}

static gsize
z_stream_fd_extra_save_method(ZStream *s, gpointer extra)
{
  ZStreamFDExtra *fd_extra;
  gsize ofs;

  ofs = Z_SUPER(s, ZStream)->extra_save(s, extra);

  fd_extra = (ZStreamFDExtra *) (((gchar *) extra) + ofs);
  fd_extra->nonblock = z_stream_get_nonblock(s);
  return ofs + sizeof(ZStreamFDExtra);
}

static gsize
z_stream_fd_extra_restore_method(ZStream *s, gpointer extra)
{
  ZStreamFDExtra *fd_extra;
  gsize ofs;

  ofs = Z_SUPER(s, ZStream)->extra_restore(s, extra);

  fd_extra = (ZStreamFDExtra *) (((gchar *) extra) + ofs);
  z_stream_set_nonblock(s, fd_extra->nonblock);
  return ofs + sizeof(ZStreamFDExtra);
}

/**
 * Allocate and initialize a ZStreamFD instance with the given fd and name.
 *
 * @param[in] fd fd to wrap this stream around
 * @param[in] name name to identify to stream in logs
 *
 * @returns ZStream instance
 **/
ZStream *
z_stream_fd_new(gint fd, const gchar *name)
{
  ZStreamFD *self;

  z_enter();
  self = Z_CAST(z_stream_new(Z_CLASS(ZStreamFD), name, G_IO_IN|G_IO_OUT), ZStreamFD);
  self->fd = fd;
#ifdef G_OS_WIN32
  self->winsock_event = (int)WSACreateEvent();
  self->channel = g_io_channel_win32_new_socket(fd);
#else
  self->channel = g_io_channel_unix_new(fd);
#endif
  self->keepalive = 0;
  self->shutdown = FALSE;
  g_io_channel_set_encoding(self->channel, NULL, NULL);
  g_io_channel_set_buffered(self->channel, FALSE);
  g_io_channel_set_close_on_unref(self->channel, FALSE);
  z_return(&self->super);
}

/** destructor */
static void
z_stream_fd_free_method(ZObject *s)
{
  ZStreamFD *self = Z_CAST(s, ZStreamFD);

  z_enter();
#ifdef G_OS_WIN32
  z_trace(NULL, "WinSock: Event #%d destroyed at %s line %d", self->winsock_event, __FILE__, __LINE__);
  WSACloseEvent(reinterpret_cast<HANDLE>(self->winsock_event));
#endif
  g_io_channel_unref(self->channel);
  z_stream_free_method(s);
  z_return();
}

/**
 * ZStreamFD virtual methods.
 **/
static ZStreamFuncs
z_stream_fd_funcs =
{
  {
    Z_FUNCS_COUNT(ZStream),
    z_stream_fd_free_method
  },
  z_stream_fd_read_method,
  z_stream_fd_write_method,
  NULL,
  z_stream_fd_write_pri_method,
  z_stream_fd_shutdown_method,
  z_stream_fd_close_method,
  z_stream_fd_ctrl_method,

  z_stream_fd_attach_source_method,
  NULL, /* detach_source */
  z_stream_fd_watch_prepare,
  z_stream_fd_watch_check,
  z_stream_fd_watch_dispatch,
  NULL, /* watch_finalize */

  z_stream_fd_extra_get_size_method,
  z_stream_fd_extra_save_method,
  z_stream_fd_extra_restore_method,
  NULL,
  NULL
};

/**
 * ZStreamFD class descriptor.
 **/
Z_CLASS_DEF(ZStreamFD, ZStream, z_stream_fd_funcs);
