/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/socketsource.h>
#include <zorpll/log.h>
#include <zorpll/error.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#endif

/**
 * This is the prepare function of ZSocketSource.
 *
 * @param[in]  s ZSocketSource instance
 * @param[out] timeout poll timeout
 *
 * @see GSourceFuncs documentation.
 *
 * @returns always FALSE
 **/
static gboolean
z_socket_source_prepare(GSource *s, gint *timeout)
{
  ZSocketSource *self = (ZSocketSource *) s;

  if (self->suspended)
    {
      self->poll.events = 0;
      self->poll.revents = 0;
      *timeout = -1;
      return FALSE;
    }
  else
    {
#ifdef G_OS_WIN32
      self->poll.events = G_IO_IN;
      z_trace(NULL, "WinSock: WSAEventSelect(%d,%d,%o) at %s line %d", self->fd, self->poll.fd, self->cond, __FILE__, __LINE__);
      if (WSAEventSelect(self->fd, reinterpret_cast<HANDLE>(self->poll.fd), self->cond) == SOCKET_ERROR) //(HANDLE) 
        {
          /*LOG
            This message indicates that the WinSock setup event setup failed for
            the given reason. 
           */
          z_log(NULL, CORE_ERROR, 0, "Failed to setup WinSock event; error='%s'", g_strerror(errno));
          *timeout = -1;
          return FALSE;
        }
#else
      self->poll.events = self->cond;
#endif
    }
    
  if (self->timeout_time != -1)
    {
      *timeout = (self->timeout_time - time(NULL)) * 1000;
      if (*timeout < 0)
        *timeout = 0;
    }
  else
    *timeout = -1;

  return FALSE;
}

/**
 * This is the check function of ZSocketSource
 *
 * @param[in] s ZSocketSource instance
 *
 * @see GSourceFuncs documentation for explanation
 *
 * @returns TRUE if it's ready to be dispatched
 **/
static gboolean
z_socket_source_check(GSource *s)
{
  ZSocketSource *self = (ZSocketSource *) s;
#ifdef G_OS_WIN32
  WSANETWORKEVENTS evs;
#endif

  if (self->timeout_time > 0 && time(NULL) >= self->timeout_time)
    {
      self->timed_out = TRUE;
      return TRUE;
    }
  else
    self->timed_out = FALSE;

#ifdef G_OS_WIN32
  if (self->acceptevent)
    return TRUE;
  WSAEnumNetworkEvents(self->fd, reinterpret_cast<HANDLE>(self->poll.fd), &evs); // (HANDLE) 
  self->poll.revents = (gushort)evs.lNetworkEvents;
  if(evs.lNetworkEvents != 0)
    self->acceptevent = TRUE;
  z_trace(NULL, "WinSock: Event %d on fd %d at %s line %d", self->poll.revents, self->poll.fd, __FILE__, __LINE__);
#endif

  return !!self->poll.revents;
}

/**
 * This is the dispatch function of ZSocketSource
 *
 * @param[in] s ZSocketSource instance
 * @param[in] callback callback function we're supposed to call
 * @param[in] user_data parameter for the callback function
 *
 * @see GSourceFuncs documentation for explanation
 **/
static gboolean
z_socket_source_dispatch(GSource     *s,
                         GSourceFunc  callback,
                         gpointer     user_data)
{
  ZSocketSource *self = (ZSocketSource *) s;

  z_trace(NULL, "Dispatching event for fd %d", self->poll.fd);
  if(!self->suspended)
    {
#ifdef G_OS_WIN32
      self->acceptevent = FALSE;
#endif
      return ((ZSocketSourceFunc) callback)(self->timed_out, user_data);
    }
  return TRUE;
}

/**
 * ZSocketSource's finalize function, see GSourceFuncs documentation for explanation.
 **/
void
z_socket_source_finalize(GSource *source G_GNUC_UNUSED)
{
#ifdef G_OS_WIN32
  ZSocketSource *self = (ZSocketSource *) source;
  
  z_trace(NULL, "WinSock: Event #%d destroyed at %s line %d",self->poll.fd, __FILE__, __LINE__);
  WSACloseEvent(reinterpret_cast<HANDLE>(self->poll.fd)); //(HANDLE)
#endif
}

/**
 * ZSocketSource virtual methods.
 **/
GSourceFuncs z_socket_source_funcs = 
{
  z_socket_source_prepare,
  z_socket_source_check,
  z_socket_source_dispatch,
  z_socket_source_finalize,
  NULL,
  NULL
};

void
z_socket_source_suspend(GSource *s)
{
  ZSocketSource *self = (ZSocketSource *) s;

  z_enter();
  self->suspended = TRUE;
  z_return();
}

void
z_socket_source_resume(GSource *s)
{
  ZSocketSource *self = (ZSocketSource *) s;
  
  z_enter();
  self->suspended = FALSE;
  z_return();
}


GSource *
z_socket_source_new(gint fd, GIOCondition cond, gint timeout)
{
  ZSocketSource *self;
  
  self = (ZSocketSource *) g_source_new(&z_socket_source_funcs, sizeof(ZSocketSource));

#ifdef G_OS_WIN32
  self->fd = fd;
  self->poll.fd = (int)WSACreateEvent(); //by Abi
#else
  self->poll.fd = fd;
#endif
  self->cond = cond;
  g_source_add_poll(&self->super, &self->poll);
  g_source_set_can_recurse(&self->super, FALSE);
  if (timeout != -1)
    self->timeout_time = time(NULL) + timeout;
  else
    self->timeout_time = -1;
    
  return &self->super;
}
