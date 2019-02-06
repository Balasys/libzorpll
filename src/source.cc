/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/source.h>
#include <zorpll/log.h>

#include <glib.h>

/**
 * Miscellaneous source objects for use throughout the ZMS
 **/
typedef struct _ZThresholdSource
{
  GSource super;
  guint idle_threshold;
  guint busy_threshold;
  time_t last_call;
  time_t start_time;
} ZThresholdSource;

/**
 * prepare() function for the threshold source
 *
 * @param[in]  s source object
 * @param[out] timeout returns the calculated timeout here
 *
 * @returns always FALSE
 **/
static gboolean
z_threshold_source_prepare(GSource *s, gint *timeout)
{
  ZThresholdSource *self = (ZThresholdSource *)s;
  time_t now;
  
  now = time(NULL);
  self->start_time = now;
  
  *timeout = MIN(self->idle_threshold,
                 self->busy_threshold + self->last_call - now) * 1000;
  
  return FALSE;
}

/**
 * check() function for the threshold source
 *
 * @param[in] s source object
 **/
static gboolean
z_threshold_source_check(GSource *s)
{
  ZThresholdSource *self = (ZThresholdSource *) s;
  time_t now;
  gboolean ret;
  
  z_enter();
  now = time(NULL);
  ret = ((time_t)(self->start_time + self->idle_threshold) <= now);
  ret = ret || ((time_t)(self->last_call + self->busy_threshold) <= now);
  z_return(ret);
}

/**
 * dispatch() function for the threshold source
 *
 * @param[in] s source object
 * @param[in] callback callback function associated with the source
 * @param[in] user_data pointer to be passed to the callback function
 *
 * @returns FALSE if the callback function was not set; whatever the callback function returned otherwise.
 **/
static gboolean
z_threshold_source_dispatch(GSource     *s,
                            GSourceFunc  callback,
                            gpointer     user_data)
{
  ZThresholdSource *self = (ZThresholdSource *)s;
  gboolean rc = FALSE;

  z_enter();
  if (callback != NULL)
    {
      rc =  (*callback) (user_data);
      self->last_call = time(NULL);
    }
  else
    {
      /*LOG
        This message indicates an internal error. Please report this error
        to the QA team.
       */
      z_log(NULL, CORE_ERROR, 4, "Threshold callback function not set;");
    }
  z_return(rc);
}

/**
 * ZThresholdSource virtual methods.
 **/
static GSourceFuncs 
z_threshold_source_funcs = 
{
  z_threshold_source_prepare,
  z_threshold_source_check,
  z_threshold_source_dispatch,
  NULL,
  NULL,
  NULL
};

/**
 * Creates new ZThresholdSource instance.
 *
 * @param[in] idle_threshold idle threshold
 * @param[in] busy_threshold busy threshold
 * 
 * @returns new ZThresholdSource instance
 **/
GSource *
z_threshold_source_new(guint idle_threshold, guint busy_threshold)
{
  ZThresholdSource *self;
  
  self = (ZThresholdSource *) g_source_new(&z_threshold_source_funcs, sizeof(ZThresholdSource));
  self->idle_threshold = idle_threshold;
  self->busy_threshold = busy_threshold;
  return &self->super;
}

/**
 * This function changes the thresholds associated with the threshold source.
 *
 * @param[in] source the threshold source instance
 * @param[in] idle_threshold new idle threshold
 * @param[in] busy_threshold new busy threshold
 **/
void
z_threshold_source_set_threshold(GSource *source, guint idle_threshold, guint busy_threshold)
{
  ZThresholdSource *self = (ZThresholdSource *) source;
  
  self->idle_threshold = idle_threshold;
  self->busy_threshold = busy_threshold;
}

/**
 * GSource descendant class to generate an event on timeout.
 *
 * The timeout can be disabled by setting timeout_target.tv_sec = timeout_target.tv_usec = 0
 **/
typedef struct _ZTimeoutSource
{
  GSource super;
  GTimeVal timeout_target;      /**< When the timeout will expire. */
} ZTimeoutSource;

/**
 * Checks if the ZTimeoutSource instance is enabled.
 *
 * @param[in] self the ZTimeoutSource instance
 *
 * Disabled state is shown by setting timeout_target.tv_sec = timeout_target.tv_usec = 0
 **/
static gboolean
z_timeout_source_enabled(ZTimeoutSource *self)
{
  if (self->timeout_target.tv_sec > 0)
    return TRUE;
  else if (self->timeout_target.tv_sec < 0)
    return FALSE;
  else
    return self->timeout_target.tv_usec > 0;
}

/**
 * prepare() function for the timeout source
 *
 * @param[in]  s source object
 * @param[out] timeout returns the calculated timeout here
 **/
static gboolean
z_timeout_source_prepare(GSource *s, gint *timeout)
{
  ZTimeoutSource *self = (ZTimeoutSource *)s;
  GTimeVal now;
  
  if (!z_timeout_source_enabled(self))
    return FALSE;

  g_get_current_time(&now);
  if (g_time_val_compare(&self->timeout_target, &now) <= 0)
    return TRUE;
  else if (timeout)
    *timeout = g_time_val_diff(&self->timeout_target, &now) / 1000;
  
  return FALSE;
}

/**
 * check() function for the timeout source
 *
 * @param[in] s source object
 **/
static gboolean
z_timeout_source_check(GSource *s)
{
  ZTimeoutSource *self = (ZTimeoutSource *) s;
  GTimeVal now;
  
  if (!z_timeout_source_enabled(self))
    return FALSE;

  g_get_current_time(&now);
  return g_time_val_compare(&self->timeout_target, &now) <= 0;
}

/**
 * dispatch() function for the timeout source
 *
 * @param          s source object (not used)
 * @param[in]      callback callback function associated with the source
 * @param[in]      user_data pointer to be passed to the callback function
 **/
static gboolean
z_timeout_source_dispatch(GSource     * /* s */,
                          GSourceFunc  callback,
                          gpointer     user_data)
{
  return (*callback)(user_data);
}

/**
 * ZTimeoutSource virtual methods.
 **/
static GSourceFuncs z_timeout_source_funcs = 
{
  z_timeout_source_prepare,
  z_timeout_source_check,
  z_timeout_source_dispatch,
  NULL,
  NULL,
  NULL
};

/**
 * This function changes the timeout associated to the timeout source
 * pointed to by source.
 *
 * @param[in] source a reference to the timeout source
 * @param[in] new_timeout the new timeout value in milliseconds
 **/
void
z_timeout_source_set_timeout(GSource *source, gulong new_timeout)
{
  ZTimeoutSource *self = (ZTimeoutSource *) source;
  
  g_get_current_time(&self->timeout_target);
  self->timeout_target.tv_sec += new_timeout / 1000;
  g_time_val_add(&self->timeout_target, (new_timeout % 1000) * 1000);
}

/**
 * This function changes when the callback should be called next time.
 * 
 * @param[in] source a reference to the timeout source
 * @param[in] nexttime the next time the callback should be called
 **/
void
z_timeout_source_set_time(GSource *source, GTimeVal *nexttime)
{
  ZTimeoutSource *self = (ZTimeoutSource *) source;
  
  self->timeout_target.tv_sec = nexttime->tv_sec;
  self->timeout_target.tv_usec = nexttime->tv_usec;
}

/**
 * This function disables the timeout source pointed to by source.
 *
 * @param[in] source a reference to the timeout source
 **/
void
z_timeout_source_disable(GSource *source)
{
  ZTimeoutSource *self = (ZTimeoutSource *) source;
  
  self->timeout_target.tv_sec = -1;
}

/**
 * This function creates and initializes a source which issues a callback
 * when a given timeout elapses.
 *
 * @param[in] initial_timeout the initial timeout value in milliseconds
 *
 * It does not matter how many times the poll() loop runs as the time is
 * saved as the target time instead of an interval which would start each
 * time the poll loop is run.
 **/
GSource *
z_timeout_source_new(gulong initial_timeout)
{
  ZTimeoutSource *self;
  
  self = (ZTimeoutSource *) g_source_new(&z_timeout_source_funcs, sizeof(ZTimeoutSource));
  z_timeout_source_set_timeout((GSource*)self, initial_timeout);
  return &self->super;
}

