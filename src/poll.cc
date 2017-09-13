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

#include <zorpll/poll.h>
#include <zorpll/stream.h>
#include <zorpll/log.h>
#include <zorpll/source.h>
#include <zorpll/error.h>

#include <glib.h>

#include <sys/types.h>
#ifndef G_OS_WIN32
#  include <sys/poll.h>
#endif
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <assert.h>

/**
 * @file
 *
 * @todo FIXME: this module should be discarded and GMainContext should be used
 * directly instead. -- Bazsi, 2006.08.11. 
 **/

/**
 * ZRealPoll is an implementation of the ZPoll interface defined in
 * poll.h. It's a callback based poll loop, which can be used by proxy
 * modules. It holds a collection of ZStream objects and calls their
 * callbacks when a requested I/O event occurs. ZPoll objects are
 * reference counted and are automatically freed when the number of
 * references to a single instance reaches zero.
 **/
typedef struct _ZRealPoll
{
  guint ref_count;
  GMainContext *context;
  GPollFD *pollfd;
  guint pollfd_num;
  gboolean quit;
  GStaticMutex lock;
  GSource *wakeup;
} ZRealPoll;

/**
 * This class is a special source used to wake up a ZPoll loop from
 * different threads.
 **/
typedef struct _ZPollSource
{
  GSource super;
  gboolean wakeup;
} ZPollSource;

/**
 * This is the prepare function of ZPollSource.
 *
 * @param[in]  s ZPollSource instance
 * @param[out] timeout poll timeout
 *
 * This is the prepare function of ZPollSource, it basically checks whether
 * the loop was woken up by checking self->wakeup, and returns TRUE in that
 * case.
 *
 * @see GSourceFuncs documentation.
 *
 * @returns always -1 timeout value, e.g. infinite timeout
 **/
static gboolean
z_poll_source_prepare(GSource *s, gint *timeout)
{
  ZPollSource *self = (ZPollSource *)s;
    
  z_enter();
  if (self->wakeup)
    z_return(TRUE);

  *timeout = -1;
  z_return(FALSE);
}

/**
 * This is the check function of ZPollSource.
 *
 * @param s ZPollSource instance (not used)
 *
 * As we poll nothing we always return FALSE.
 *
 * @see GSourceFuncs documentation.
 *
 * @returns FALSE
 *
 * @todo FIXME: I think this function should check the value of self->wakeup
 **/
static gboolean
z_poll_source_check(GSource *s G_GNUC_UNUSED)
{
  z_enter();
  z_return(FALSE);
}

/**
 * This function simply sets self->wakeup to FALSE to allow the next poll loop
 * to run.
 *
 * @param[in] s ZPollSource instance
 * @param     callback the callback for s (not used)
 * @param     user_data the data to be passed to callback (not used)
 *
 * @see GSourceFuncs documentation.
 *
 * @returns TRUE
 **/
static gboolean
z_poll_source_dispatch(GSource *s,
                       GSourceFunc  callback G_GNUC_UNUSED,
                       gpointer  user_data G_GNUC_UNUSED)
{
  ZPollSource *self = (ZPollSource *) s;
  
  z_enter();
  self->wakeup = FALSE;
  z_return(TRUE);
}

/**
 * ZPollSource virtual methods.
 **/
GSourceFuncs z_poll_source_funcs = 
{
  z_poll_source_prepare,
  z_poll_source_check,
  z_poll_source_dispatch,
  NULL,
  NULL,
  NULL
};

/**
 * This function creates a new ZPoll instance.
 *
 * @returns a pointer to the new instance
 **/
ZPoll *
z_poll_new(void)
{
  ZRealPoll *self = g_new0(ZRealPoll, 1);
  
  z_enter();
  g_return_val_if_fail( self != NULL, NULL);
  self->ref_count = 1;
  self->quit = FALSE;
  self->pollfd_num = 4;
  self->pollfd = g_new(GPollFD, self->pollfd_num);
  self->context = g_main_context_default();
  if (g_main_context_acquire(self->context))
    {
      g_main_context_ref(self->context);
    }
  else
    {
      self->context = g_main_context_new();
      assert(g_main_context_acquire(self->context));
    }
  self->wakeup = g_source_new(&z_poll_source_funcs,
                              sizeof(ZPollSource));
  g_source_attach(self->wakeup, self->context);
  z_return((ZPoll *) self);
}

/** 
 * Used internally to free up an instance when the reference count
 * reaches 0.
 *
 * @param[in] s ZPoll instance
 **/
static void
z_poll_destroy(ZPoll *s)
{
  ZRealPoll *self = (ZRealPoll *) s;

  z_enter();
  if(self->wakeup)
    {
      g_source_destroy(self->wakeup);
      g_source_unref(self->wakeup);
      self->wakeup = NULL;
    }
  g_main_context_release(self->context);
  g_main_context_unref(self->context);
  g_free(self->pollfd);
  g_free(self);
  z_return();
}

/**
 * Increment the reference count of the given ZPoll instance.
 *
 * @param[in] s ZPoll instance
 **/
void 
z_poll_ref(ZPoll *s)
{
  ZRealPoll *self = (ZRealPoll *) s;
  
  z_enter();
  self->ref_count++;
  z_return();
}


/**
 * Decrement the reference count of the given ZPoll instance, and free it
 * using z_poll_destroy() if it reaches 0.
 *
 * @param[in] s ZPoll instance
 **/
void
z_poll_unref(ZPoll *s)
{
  ZRealPoll *self = (ZRealPoll *) s;

  z_enter();
  if (self)
    {
      g_assert(self->ref_count > 0);
      self->ref_count--;
      if (self->ref_count == 0)
        z_poll_destroy(s);
    }
  z_return();
}

/**
 * Register a ZStream to be monitored by a ZPoll instance.
 *
 * @param[in] s ZPoll instance
 * @param[in] stream stream instance
 **/
void
z_poll_add_stream(ZPoll *s, ZStream *stream)
{
  ZRealPoll *self = (ZRealPoll *) s;

  z_enter();
  z_stream_attach_source(stream, self->context);
  z_return();
}

/**
 * Remove a ZStream from a ZPoll instance.
 *
 * @param[in] s ZPoll instance
 * @param[in] stream stream to be removed
 **/
void
z_poll_remove_stream(ZPoll *s G_GNUC_UNUSED, ZStream *stream)
{
  z_enter();
  z_stream_detach_source(stream);
  z_return();
}

/**
 * Run an iteration of the poll loop.
 *
 * @param[in] s ZPoll instance
 * @param[in] timeout timeout value in milliseconds
 *
 * This function runs an iteration of the poll loop. Monitor filedescriptors of
 * registered Streams, and call appropriate callbacks.
 *
 * @returns TRUE if the iteration should be called again.
 **/
guint
z_poll_iter_timeout(ZPoll *s, gint timeout)
{
  ZRealPoll *self = (ZRealPoll *) s;
  gint max_priority = G_PRIORITY_LOW;
  gint polltimeout;
  gint fdnum = 0;
  GPollFunc pollfunc;
  gint rc;

  z_enter();
  z_errno_set(0);
  if (self->quit)
    z_return(0);
  g_main_context_prepare (self->context, &max_priority);
  fdnum = g_main_context_query(self->context,
                               max_priority,
                               &polltimeout,
                               self->pollfd,
                               self->pollfd_num);

  while (fdnum > (gint)self->pollfd_num)
    {
      /*LOG
        This message reports that the polling fd structure is growing.
       */
      z_log(NULL, CORE_DEBUG, 7, "Polling fd structure growing; old_num='%d'", self->pollfd_num);
      self->pollfd_num *= 2;
      self->pollfd = g_renew(GPollFD, self->pollfd, self->pollfd_num);
      fdnum = g_main_context_query(self->context,
                                   max_priority,
                                   &polltimeout,
                                   self->pollfd,
                                   self->pollfd_num);
    }

  if (polltimeout <= -1)
    polltimeout = timeout;
  else if (timeout > -1)
    polltimeout = MIN(polltimeout, timeout);

  pollfunc = g_main_context_get_poll_func(self->context);
  z_trace(NULL, "Entering poll;");
  rc = pollfunc(self->pollfd, fdnum, polltimeout);
  z_trace(NULL, "Returning from poll;");

  if (g_main_context_check(self->context, max_priority, self->pollfd, fdnum))
    g_main_context_dispatch(self->context);

  if (rc == -1 && !z_errno_is(EINTR))
    z_return(0);

  if (rc == 0 && polltimeout == timeout)
    {
      z_errno_set(ETIMEDOUT);
      z_return(0);
    }
  z_return(1);
}

/**
 * Wake up a running poll loop using its wakeup pipe.
 *
 * @param[in] s ZPoll instance
 **/
void
z_poll_wakeup(ZPoll *s)
{
  ZRealPoll *self = (ZRealPoll *) s;
  ZPollSource *src;
  
  z_enter();
  src = (ZPollSource *)self->wakeup;
  src->wakeup = TRUE;
  g_main_context_wakeup(self->context);
  z_return();
}

/**
 * Checks whether z_poll_quit was called earlier on this ZPoll object.
 *
 * @param[in] s ZPoll instance
 *
 * @returns TRUE if the poll is still running
 **/
gboolean
z_poll_is_running(ZPoll *s)
{
  ZRealPoll *self = (ZRealPoll *) s;
  
  z_enter();
  z_return(!self->quit);
}

/**
 * Indicate that this poll loop is to be ended.
 * 
 * @param[in] s ZPoll instance
 *
 * Can be called from thread different from the one running poll.
 **/
void
z_poll_quit(ZPoll *s)
{
  ZRealPoll *self = (ZRealPoll *) s;  
  
  z_enter();
  self->quit = TRUE;
  z_poll_wakeup(s);
  z_return();
}

/**
 * Return the underlying GMainContext.
 *
 * @param[in] s ZPoll instance
 **/
GMainContext *
z_poll_get_context(ZPoll *s)
{
  ZRealPoll *self = (ZRealPoll *) s;
  
  z_enter();
  z_return(self->context);
}
