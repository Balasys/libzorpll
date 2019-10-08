/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/stream.h>
#include <zorpll/log.h>
#include <zorpll/error.h>
#include <zorpll/packetbuf.h>
#include <zorpll/io.h>

#include <string.h>
#include <sys/types.h>

/* source functions delegating prepare/check/dispatch/finalize to the stream
 * methods. */

/* GSource derived class used to implement ZStream callbacks */

/**
 * @file
 *
 * <h1>Structure of streams</h1>
 * <!--====================-->
 *
 * Streams are cooperating, full-duplex pipes which can be "stacked" to each
 * other to create another, more complex stream. For example we can use
 * the following structure:
 *
 * <pre>
 *    ZStreamLine <-> ZStreamGzip <-> ZStreamSsl <-> ZStreamFd
 * </pre>
 *
 * In which case we can process an encrypted/compressed stream by lines.
 * Each stream can be the 'top' of the stack, but every individual stream
 * can only be in a single stack at a time. It is forbidden to address a
 * below the stream top directly.
 *
 * Each stream can be used in blocking and non-blocking fashion. The user is
 * free to register I/O callbacks to get notifications on the availability
 * of data.
 *
 * <h1>Reference counts</h1>
 * <!--================-->
 *
 * ZStreams are ZObjects, thus they are reference counted objects. However
 * in addition to the stream structure GSource objects are used to implement
 * the functionality. In reality references look something like below:
 *
 *
 * <pre>
 *  Application ---------+
 *                       |
 *                       ˇ
 *                     +----------+     +-------------+
 *                     |stream top| <-> |stream source|
 *                     +----------+     +-------------+
 *                       |  ^
 *                       ˇ  |
 *                     +----------+     +-------------+
 *                     |stream 1  | <-> |stream source|
 *                     +----------+     +-------------+
 *                       |  ^
 *                       ˇ  |
 *                     +----------+     +-------------+
 *                     |stream 2  | <-> |stream source|
 *                     +----------+     +-------------+
 *                       ...
 *                       |  ^
 *                       ˇ  |
 *                     +----------+     +-------------+
 *                     |stream bot| <-> |stream source|
 *                     +----------+     +-------------+
 * </pre>
 *
 * There are several circles in the reference counting model, thus we need
 * explicit reference breaking points. The circles are:
 *   - streams reference their children and their parent
 *   - streams reference their accompanying source and vica versa
 *
 * The internal structure of the stream stack is called 'structure' and has
 * a separate reference counter from the stream's main refcount.
 *
 * Destruction happens in three phases, the order of the first two is not
 * defined, it is up to the application which order it chooses to destruct
 * its streams:
 *   - close it by calling z_stream_close(),
 *   - detach the source by calling z_poll_remove_stream(), or
 *     z_stream_detach(), or by returning FALSE from the dispatch callback
 *   - unref the stream top
 *
 * The stream stack beneath the stream top will be freed right after the
 * reference counter of stream top's structure reaches zero, the stream top
 * itself will be freed when the application calls its final z_stream_unref().
 **/

static void z_stream_struct_ref(ZStream *self);
static void z_stream_struct_unref(ZStream *self);

/**
 * GSource-derived event source for a stream.
 **/
struct _ZStreamSource
{
  GSource super;
  ZStream *stream;
};
G_LOCK_DEFINE_STATIC(detach_lock);

/**
 * Check whether user callbacks can be called at all.
 *
 * @param[in] self this
 * @param[in] in_call FIXME: I don't know what this does.
 *
 * This function is used in all stream callbacks to check whether user
 * callbacks can be called at all. It currently checks whether the call is
 * recursive, or whether the source was already destroyed with
 * g_source_destroy(). The latter requires locking as g_source_destroy()
 * does not ensure that no further callbacks will be delivered. We are using
 * the global detach_lock for this purpose.
 *
 * We are also doing a z_stream_struct_ref() within the protection of the
 * lock.
 *
 * @note to be absolutely general we should check the return value for
 * g_source_get_can_recurse(), however the ZStream code creates this GSource
 * and currently it never uses recursive sources. If that assumption
 * changes, we need to properly check the recursion state.
 **/
static inline gboolean
z_stream_source_grab_ref(ZStreamSource *self, gboolean in_call, ZStream **top_stream)
{
  gboolean res = FALSE;
  ZStream *p;

  /* NOTE: no z_enter() on purpose, it would generate a _lot_ of logs */

  G_LOCK(detach_lock);
  for (p = self->stream; p; p = p->parent)
    {
      ZStreamSource *source = (ZStreamSource *) p->source;

      if (!source || (source->super.flags & ((in_call ? 0 : G_HOOK_FLAG_IN_CALL) + G_HOOK_FLAG_ACTIVE)) != G_HOOK_FLAG_ACTIVE)
        {
          /* one of the streams has a pending destruction, bail out and don't call user callbacks */
          G_UNLOCK(detach_lock);
          return FALSE;
        }
      /* NOTE: in_call only needs to be consulted for the stream that we were called with */
      in_call = FALSE;
      *top_stream = p;
    }
  z_stream_struct_ref(*top_stream);
  res = TRUE;
  G_UNLOCK(detach_lock);
  return res;
}

static gboolean
z_stream_source_prepare(GSource *s, gint *timeout)
{
  ZStreamSource *self = (ZStreamSource *) s;
  gboolean ret = FALSE;
  ZStream *top_stream = NULL;

  z_enter();
  if (!z_stream_source_grab_ref(self, FALSE, &top_stream))
    z_return(FALSE);

  if (self->stream->want_read && self->stream->ungot_bufs)
    {
      *timeout = 0;
      ret = TRUE;
    }
  else
    {
      ret = z_stream_watch_prepare(self->stream, s, timeout);
    }
  z_stream_struct_unref(top_stream);
  z_return(ret);
}

static gboolean
z_stream_source_check(GSource *s)
{
  ZStreamSource *self = (ZStreamSource *) s;
  gboolean ret = FALSE;
  ZStream *top_stream = NULL;

  z_enter();
  if (!z_stream_source_grab_ref(self, FALSE, &top_stream))
    z_return(FALSE);

  if (self->stream->want_read && self->stream->ungot_bufs)
    ret = TRUE;
  else
    ret = z_stream_watch_check(self->stream, s);
  z_stream_struct_unref(top_stream);
  z_return(ret);
}

static gboolean
z_stream_source_dispatch(GSource     *s,
                         GSourceFunc /* callback */,
                         gpointer    /* user_data */)
{
  ZStreamSource *self = (ZStreamSource *) s;
  gboolean ret = FALSE;
  ZStream *top_stream = NULL;

  z_enter();

  if (!z_stream_source_grab_ref(self, TRUE, &top_stream))
    {
      /* NOTE: we are returning TRUE as we come here only when the
       * destruction of this source has already been requested.
       */
      z_return(TRUE);
    }

  if (self->stream->want_read && self->stream->ungot_bufs)
    ret = self->stream->read_cb(self->stream, G_IO_IN, self->stream->user_data_read);
  else
    ret = z_stream_watch_dispatch(self->stream, s);

  if (!ret)
    {
      /* NOTE: top_stream here is only a borrowed reference which might be
       * freed during z_stream_detach_source causing an abort or segfault.
       * Therefore we need to ref it to ensure a proper reference is passed
       * to the functions in the call chain.
       */

      /* NOTE/2: I'm not sure that the NOTE above still applies, but ref/unref here won't hurt */

      z_stream_ref(top_stream);
      z_stream_detach_source(top_stream);
      z_stream_unref(top_stream);
    }
  z_stream_struct_unref(top_stream);
  z_return(ret);
}

static void
z_stream_source_finalize(GSource *s)
{
  ZStreamSource *self = (ZStreamSource *) s;
  ZStream *stream;

  z_enter();

  /* NOTE: we don't call _struct_ref here as we are only called when that
   * already dropped to zero */
  z_stream_watch_finalize(self->stream, s);

  stream = self->stream;
  self->stream = NULL;
  z_stream_unref(stream);
  z_return();
}

/**
 * ZStreamSource virtual methods.
 **/
static GSourceFuncs
z_stream_source_funcs =
{
  z_stream_source_prepare,
  z_stream_source_check,
  z_stream_source_dispatch,
  z_stream_source_finalize,
  NULL,
  NULL
};

/**
 * Create a new ZStreamSource instance.
 *
 * @param[in] stream ZStream to associate with the ZStreamSource
 *
 * The new object will hold a reference to the ZStream and so the
 * reference count of the ZStream will be incremented.
 *
 * @return new object
 **/
GSource *
z_stream_source_new(ZStream *stream)
{
  ZStreamSource *self = (ZStreamSource *) g_source_new(&z_stream_source_funcs, sizeof(ZStreamSource));

  z_enter();
  z_stream_ref(stream);
  self->stream = stream;
  z_return(&self->super);
}

/* ZStreamContext */

/**
 * ZStreamContext destructor.
 *
 * @param[in] self ZStreamContext instance
 *
 * The GDestroyNotify callbacks will only be called if their user data pointers are also set.
 **/
void
z_stream_context_destroy(ZStreamContext *self)
{
  z_enter();
  if (!self->restored)
    {
      if (self->user_data_read && self->user_data_read_notify)
        self->user_data_read_notify(self->user_data_read);
      if (self->user_data_write && self->user_data_write_notify)
        self->user_data_write_notify(self->user_data_write);
      if (self->user_data_pri && self->user_data_pri_notify)
        self->user_data_pri_notify(self->user_data_pri);
      g_free(self->stream_extra);
      self->stream_extra = NULL;
      self->restored = TRUE;
    }
  z_return();
}

/**
 * Structure used for querying/changing ZStream I/O callbacks.
 **/
typedef struct _ZStreamSetCb
{
  ZStreamCallback cb;
  gpointer cb_data;
  GDestroyNotify cb_notify;
} ZStreamSetCb;

/**
 * Increment the reference count of a ZStream instance.
 *
 * @param[in] self ZStream instance
 **/
static void
z_stream_struct_ref(ZStream *self)
{
  z_refcount_inc(&self->struct_ref);
}

/**
 * Decrement the reference count of a ZStream instance.
 *
 * @param[in] self ZStream instance
 *
 * Also breaks circular references to its child.
 **/
static void
z_stream_struct_unref(ZStream *self)
{
  if (z_refcount_dec(&self->struct_ref))
    {
      ZStream *child;

      /* break circular reference to child */
      child = self->child;
      if (child)
        {
          z_stream_ref(child);
          z_stream_set_child(self, NULL);
          z_stream_unref(child);
        }
    }
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
gboolean
z_stream_ctrl_method(ZStream *s, guint function, gpointer value, guint vlen)
{
  gboolean res = FALSE;

  z_enter();
  switch (ZST_CTRL_MSG(function))
    {
    case ZST_CTRL_GET_COND_READ:
      if (vlen == sizeof(gboolean))
        {
          *(gboolean *)value = s->want_read;
          res = TRUE;
        }
      break;

    case ZST_CTRL_SET_COND_READ:
      if (vlen == sizeof(gboolean))
        {
          s->want_read =  *((gboolean *)value);
          res = TRUE;
        }
      break;

    case ZST_CTRL_GET_COND_WRITE:
      if (vlen == sizeof(gboolean))
        {
          *(gboolean *)value = s->want_write;
          res = TRUE;
        }
      break;

    case ZST_CTRL_SET_COND_WRITE:
      if (vlen == sizeof(gboolean))
        {
          s->want_write = *((gboolean *)value);
          res = TRUE;
        }
      break;

    case ZST_CTRL_GET_COND_PRI:
      if (vlen == sizeof(gboolean))
        {
          *(gboolean *)value = s->want_pri;
          res = TRUE;
        }
      break;

    case ZST_CTRL_SET_COND_PRI:
      if (vlen == sizeof(gboolean))
        {
          s->want_pri = *((gboolean *)value);
          res = TRUE;
        }
      break;

    case ZST_CTRL_GET_CALLBACK_READ:
      if (vlen == sizeof(ZStreamSetCb))
        {
          ZStreamSetCb *cbv = (ZStreamSetCb *)value;
          cbv->cb = s->read_cb;
          cbv->cb_data = s->user_data_read;
          cbv->cb_notify = s->user_data_read_notify;
          res = TRUE;
        }
      break;

    case ZST_CTRL_SET_CALLBACK_READ:
      if (vlen == sizeof(ZStreamSetCb))
        {
          ZStreamSetCb *cbv = (ZStreamSetCb *)value;

          if (s->user_data_read_notify)
            s->user_data_read_notify(s->user_data_read);
          s->read_cb = cbv->cb;
          s->user_data_read = cbv->cb_data;
          s->user_data_read_notify = cbv->cb_notify;
          res = TRUE;
        }
      break;

    case ZST_CTRL_GET_CALLBACK_WRITE:
      if (vlen == sizeof(ZStreamSetCb))
        {
          ZStreamSetCb *cbv = (ZStreamSetCb *)value;
          cbv->cb = s->write_cb;
          cbv->cb_data = s->user_data_write;
          cbv->cb_notify = s->user_data_write_notify;
          res = TRUE;
        }
      break;

    case ZST_CTRL_SET_CALLBACK_WRITE:
      if (vlen == sizeof(ZStreamSetCb))
        {
          ZStreamSetCb *cbv = (ZStreamSetCb *)value;

          if (s->user_data_write_notify)
            s->user_data_write_notify(s->user_data_write);
          s->write_cb = cbv->cb;
          s->user_data_write = cbv->cb_data;
          s->user_data_write_notify = cbv->cb_notify;
          res = TRUE;
        }
      break;

    case ZST_CTRL_GET_CALLBACK_PRI:
      if (vlen == sizeof(ZStreamSetCb))
        {
          ZStreamSetCb *cbv = (ZStreamSetCb *)value;
          cbv->cb = s->pri_cb;
          cbv->cb_data = s->user_data_pri;
          cbv->cb_notify = s->user_data_pri_notify;
          res = TRUE;
        }
      break;

    case ZST_CTRL_SET_CALLBACK_PRI:
      if (vlen == sizeof(ZStreamSetCb))
        {
          ZStreamSetCb *cbv = (ZStreamSetCb *)value;

          if (s->user_data_pri_notify)
            s->user_data_pri_notify(s->user_data_pri);
          s->pri_cb = cbv->cb;
          s->user_data_pri = cbv->cb_data;
          s->user_data_pri_notify = cbv->cb_notify;
          res = TRUE;
        }
      break;

    case ZST_CTRL_SET_TIMEOUT_BLOCK:
      if (vlen == sizeof(gint))
        {
          s->timeout = *((gint *)value);
          res = TRUE;
        }
      break;

    default:
      if (s->child)
        {
          res = z_stream_ctrl(s->child, function, value, vlen);
          z_return(res);
        }
      break;
    }

  if (res && (function & ZST_CTRL_MSG_FORWARD) && s->child)
    res = z_stream_ctrl(s->child, function, value, vlen);

  z_return(res);
}

/**
 * Drop read, write and priority data callbacks.
 *
 * @param[in] self ZStream instance
 *
 * The GDestroyNotify callbacks will only be called if their user data pointers are also set.
 **/
void
z_stream_drop_callbacks(ZStream *self)
{
  if (self->user_data_read && self->user_data_read_notify)
    self->user_data_read_notify(self->user_data_read);
  self->user_data_read = NULL;
  self->read_cb = NULL;
  if (self->user_data_write && self->user_data_write_notify)
    self->user_data_write_notify(self->user_data_write);
  self->user_data_write = NULL;
  self->write_cb = NULL;
  if (self->user_data_pri && self->user_data_pri_notify)
    self->user_data_pri_notify(self->user_data_pri);
  self->user_data_pri = NULL;
  self->pri_cb = NULL;
}

/**
 * This function enables or disables an I/O callback by setting the
 * appropriate want_read/want_write flags.
 *
 * @param[in] s ZStream instance
 * @param[in] type callback type (G_IO_*)
 * @param[in] value enable or disable
 *
 * @returns TRUE on success
 **/
gboolean
z_stream_set_cond(ZStream *s, guint type, gboolean value)
{
  gboolean ret = FALSE;

  switch (type)
    {
    case G_IO_IN:
      ret = z_stream_ctrl(s, ZST_CTRL_SET_COND_READ, &value, sizeof(value));
      break;

    case G_IO_OUT:
      ret = z_stream_ctrl(s, ZST_CTRL_SET_COND_WRITE, &value, sizeof(value));
      break;

    case G_IO_PRI:
      ret = z_stream_ctrl(s, ZST_CTRL_SET_COND_PRI, &value, sizeof(value));
      break;

    default:
      break;
    }
  return ret;
}

/**
 * This function returns the value of the appropriate want_read/want_write flags.
 *
 * @param[in] s ZStream instance
 * @param[out] v store actual value here; not modified if returns FALSE; NULL-tolerant
 *
 * @returns TRUE on success
 **/
gboolean
z_stream_get_cond(ZStream *s, guint type, gboolean *v)
{
  gboolean ret = FALSE;
  gboolean value;

  switch (type)
    {
    case G_IO_IN:
      ret = z_stream_ctrl(s, ZST_CTRL_GET_COND_READ, &value, sizeof(value));
      break;

    case G_IO_OUT:
      ret = z_stream_ctrl(s, ZST_CTRL_GET_COND_WRITE, &value, sizeof(value));
      break;

    case G_IO_PRI:
      ret = z_stream_ctrl(s, ZST_CTRL_GET_COND_PRI, &value, sizeof(value));
      break;

    default:
      break;
    }

  if (ret && v)
    *v = value;

  return ret;
}

/**
 * This function attaches an I/O callback function to stream.
 *
 * @param[in] s ZStream instance
 * @param[in] type callback type (G_IO_*)
 * @param[in] callback I/O callback function
 * @param[in] user_data user data to pass to callback
 * @param[in] notify destroy notify function to free user_data
 *
 * @returns TRUE on success
 **/
gboolean
z_stream_set_callback(ZStream *s, guint type, ZStreamCallback callback, gpointer user_data, GDestroyNotify notify)
{
  gboolean ret = FALSE;
  ZStreamSetCb cb;

  cb.cb = callback;
  cb.cb_data = user_data;
  cb.cb_notify = notify;

  switch(type)
    {
    case G_IO_IN:
      ret = z_stream_ctrl(s, ZST_CTRL_SET_CALLBACK_READ, &cb, sizeof(cb));
      break;
    case G_IO_OUT:
      ret = z_stream_ctrl(s, ZST_CTRL_SET_CALLBACK_WRITE, &cb, sizeof(cb));
      break;
    case G_IO_PRI:
      ret = z_stream_ctrl(s, ZST_CTRL_SET_CALLBACK_PRI, &cb, sizeof(cb));
      break;
    default:
      break;
    }
  return ret;
}

/**
 * This is the default extra_get_size method to query a stream how
 * much information it needs to store as its context when the
 * application calls z_stream_save_context().
 *
 * @param[in] s ZStream instance
 *
 * @returns the size of the extra information to be stored
 **/
static gsize
z_stream_extra_get_size_method(ZStream *s)
{
  if (s->child)
    return z_stream_extra_get_size(s->child);
  return 0;
}

/**
 * This is the default extra_save method to save stream context when
 * the application calls z_stream_save_context().
 *
 * @param[in] s ZStream instance
 * @param[in] extra extra pointer
 *
 * @returns offset where the next class can start its extra data
 **/
static gsize
z_stream_extra_save_method(ZStream *s, gpointer extra)
{
  if (s->child)
    return z_stream_extra_save(s->child, extra);
  return 0;
}

/**
 * This is the default extra_restore method to restore stream context
 * when the application calls z_stream_restore_context().
 *
 * @param[in] s ZStream instance
 * @param[in] extra extra pointer
 *
 * @returns offset where the next class can start reading its extra data
 **/
static gsize
z_stream_extra_restore_method(ZStream *s, gpointer extra)
{
  if (s->child)
    return z_stream_extra_restore(s->child, extra);
  return 0;
}

/**
 * This method is called when a new stream is stacked beneath
 * self.
 *
 * @param[in] self ZStream instance
 * @param[in] new_child new stream child
 *
 * When a new stream is stacked beneath self, this method sets
 * the appropriate references and recalculates umbrella_flags
 * and timeout values.
 * Because of the z_stream_unref(parent) @self may
 * be freed in this this function. So we cannot reparent a
 * stream easily. The caller should have a reference to it
 * to do it.
 **/
static void
z_stream_set_child_method(ZStream *self, ZStream *new_child)
{
  ZStream *p;

  if (self->child)
    {
      ZStream *parent, *child;

      g_assert(self->child->parent == self);
      /* recalculate umbrella state */
      self->child->umbrella_state = self->child->umbrella_flags;
      /* detach self->child->parent reference */
      z_stream_drop_callbacks(self->child);

      /* NOTE: the set-NULL, unref order is important, as z_stream_free
       * asserts on self->child == NULL and these unrefs might trigger a
       * z_stream_free call
       */

      parent = self->child->parent;
      child = self->child;
      self->child->parent = NULL;
      self->child = NULL;

      z_stream_struct_unref(child);

      z_stream_unref(child);
      z_stream_unref(parent);
    }

  if (new_child)
    {
      g_assert(new_child->parent == NULL);

      self->stack_depth = new_child->stack_depth + 1;
      z_stream_set_name(self, new_child->name);
      new_child->parent = z_stream_ref(self);
      self->child = z_stream_ref(new_child);
      z_stream_struct_ref(self->child);
      self->timeout = new_child->timeout;
      for (p = self; p && p->child; p = p->child)
        p->child->umbrella_state &= ~self->umbrella_flags;
    }
}

/**
 * Save the complete ZStream callback state.
 *
 * @param[in]  self ZStream instance
 * @param[out] context save stream context here
 *
 * This function can be used to save the complete ZStream callback state. It
 * is usually needed when this stream is to be used in a completely
 * different context (e.g. ZTransfer vs. ZProxy). The function saves the
 * references to user_data associated with different callbacks, e.g.
 * GDestroyNotify callbacks are called when the context is freed without
 * restoring it. The function also NULLs all fields in ZStream to make it
 * sure the ZStream will not do the same.
 *
 * @returns always TRUE
 **/
gboolean
z_stream_save_context(ZStream *self, ZStreamContext *context)
{
  gsize extra_size;

  z_enter();
  context->restored = FALSE;
  context->want_read = self->want_read;
  context->user_data_read = self->user_data_read;
  context->user_data_read_notify = self->user_data_read_notify;
  context->read_cb = self->read_cb;

  context->want_pri = self->want_pri;
  context->user_data_pri = self->user_data_pri;
  context->user_data_pri_notify = self->user_data_pri_notify;
  context->pri_cb = self->pri_cb;

  context->want_write = self->want_write;
  context->user_data_write = self->user_data_write;
  context->user_data_write_notify = self->user_data_write_notify;
  context->write_cb = self->write_cb;

  context->timeout = self->timeout;
  context->nonblocking = z_stream_get_nonblock(self);

  self->want_read = FALSE;
  self->user_data_read = NULL;
  self->user_data_read_notify = NULL;
  self->read_cb = NULL;

  self->want_pri = FALSE;
  self->user_data_pri = NULL;
  self->user_data_pri_notify = NULL;
  self->pri_cb = NULL;

  self->want_write = FALSE;
  self->user_data_write = NULL;
  self->user_data_write_notify = NULL;
  self->write_cb = NULL;

  extra_size = z_stream_extra_get_size(self);
  context->stream_extra = g_malloc0(extra_size);
  z_stream_extra_save(self, context->stream_extra);

  z_return(TRUE);
}

/**
 * This function restores the stream callback context previously saved by
 * z_stream_save_context.
 *
 * @param[in] self ZStream instance
 * @param[in] context stream context previously stored by z_stream_save_context
 *
 * @note: FIXME: What we should do if context->restored is TRUE?
 * @returns always TRUE
 **/
gboolean
z_stream_restore_context(ZStream *self, ZStreamContext *context)
{
  z_enter();

  g_return_val_if_fail(!context->restored, FALSE);

  z_stream_drop_callbacks(self);

  self->want_read = context->want_read;
  self->user_data_read = context->user_data_read;
  self->user_data_read_notify = context->user_data_read_notify;
  self->read_cb = context->read_cb;

  self->want_pri = context->want_pri;
  self->user_data_pri = context->user_data_pri;
  self->user_data_pri_notify = context->user_data_pri_notify;
  self->pri_cb = context->pri_cb;

  self->want_write = context->want_write;
  self->user_data_write = context->user_data_write;
  self->user_data_write_notify = context->user_data_write_notify;
  self->write_cb = context->write_cb;

  self->timeout = context->timeout;
  z_stream_set_nonblock(self, context->nonblocking);

  if (context->stream_extra)
    {
      z_stream_extra_restore(self, context->stream_extra);
      g_free(context->stream_extra);
      context->stream_extra = NULL;
    }

  context->restored = TRUE;
  z_return(TRUE);
}

/**
 * This function searches the stream stack for a specific stream type.
 *
 * @param[in] self ZStream instance
 * @param[in] direction I/O direction (G_IO_*)
 * @param[in] klass stream class that we are looking for
 *
 * @returns a ZStream of the requested type
 **/
ZStream *
z_stream_search_stack(ZStream *self, gint direction, ZClass *klass)
{
  ZStream *p;

  z_enter();
  for (p = self; p; p = p->child)
    {
      if (z_object_is_instance(&p->super, klass))
        z_return(p);

      if ((p->umbrella_flags & direction) == direction)
        break; /* this direction is shadowed by the current entry */
    }
  z_return(NULL);
}

/**
 * Push a new stream to the top of a stream stack.
 *
 * @param[in] self current top of the stream stack (consumed reference)
 * @param[in] new_top stream to push (this reference is returned)
 *
 * Push a new stream to the top of a stream stack by setting child-parent
 * relationship and recalculating umbrella_state. The references are
 * manipulated to make it easy for calling code to push nested streams in a
 * code described in the example section below. Please note that the
 * reference semantics is not the same as the child argument to various
 * ZStream constructors, as those increment the reference count to the
 * passed argument.
 *
 * @returns the new top of the stream stack (borrowed reference).
 *
 * Example:
 *   Stream construction using z_stream_push:
 *
 *     stream = z_stream_fd_new(0, "");
 *     stream = z_stream_push(stream, z_stream_line_new(NULL, 4096, 0));
 *     stream = z_stream_push(stream, z_stream_ssl_new(...));
 *
 *   The end result is a single reference to the stream stack in the
 *   variable 'stream' and single references to all nested streams in the
 *   stream stack. Popping streams one by one can be done using:
 *
 *     stream = z_stream_pop(stream);  // pops ZStreamSsl from the top
 *     stream = z_stream_pop(stream);  // pops ZStreamLine from the top
 *     stream = z_stream_pop(stream);  // fails and returns NULL, not freeing anything
 **/
ZStream *
z_stream_push(ZStream *self, ZStream *new_top)
{
  z_stream_set_child(new_top, self);
  z_stream_unref(self);
  return new_top;
}

/**
 * Pop the topmost stream from the stack.
 *
 * @param[in] self current top of the stream stack (consumed reference)
 *
 * Pops the topmost stream from the stack, recalculating umbrella_state as
 * it goes. See the example at z_stream_push to see how references should
 * be treated.
 *
 * @returns the new top of the stack
 **/
ZStream *
z_stream_pop(ZStream *self)
{
  ZStream *new_top = z_stream_ref(self->child);

  if (new_top)
    {
      self->umbrella_state = self->umbrella_flags;
      z_stream_set_child(self, NULL);

      new_top->umbrella_state = new_top->umbrella_flags;
      z_stream_unref(self);
    }
  return new_top;
}

/**
 * This function is called when the stream is attached to a poll loop, it
 * creates the necessary GSource instance and calls z_stream_attach() on the
 * child streams recursively.
 *
 * @param[in] self ZStream instance
 * @param[in] context main context to attach to
 *
 * @note This can only be called from a single thread.
 **/
static void
z_stream_attach_source_method(ZStream *self, GMainContext *context)
{
  z_enter();
  g_assert(self->source == NULL);

  z_stream_ref(self);
  /* NOTE: we need the structure referenced as long as the source is active */
  z_stream_struct_ref(self);
  if (self->child)
    z_stream_attach_source(self->child, context);

  self->source = z_stream_source_new(self);
  g_source_set_priority(self->source, G_PRIORITY_DEFAULT - self->stack_depth);
  g_source_attach(self->source, context);

  z_stream_unref(self);
  z_return();
}

/**
 * This function is called when the stream is detached from a poll loop.
 *
 * @param[in] self ZStream instance
 *
 * @note this can be called from a thread concurrent to the callbacks.
 **/
static void
z_stream_detach_source_method(ZStream *self)
{
  gboolean detached = FALSE;
  z_enter();

  G_LOCK(detach_lock);
  if (self->source)
    {
      GSource *source = NULL;

      source = self->source;
      self->source = NULL;

      g_source_destroy(source);
      g_source_unref(source);
      detached = TRUE;
    }
  G_UNLOCK(detach_lock);

  if (self->child)
    z_stream_detach_source(self->child);

  if (detached)
    z_stream_struct_unref(self);

  z_return();
}

/**
 * This function is called to read bytes from a stream.
 *
 * @param[in]  self ZStream instance
 * @param[in]  buf destination buffer
 * @param[in]  count size of buf
 * @param[out] bytes_read number of bytes read
 * @param[out] err error value
 *
 * This function reads from the ZPktBuf inside the ZStream and returns
 * it in buf. (The ZPktBuf is in the GList ungot_bufs)
 *
 * @returns GLib I/O status
 **/
GIOStatus
z_stream_read(ZStream *self, void *buf, gsize count, gsize *bytes_read, GError **err)
{
  GIOStatus res;
  GError *local_error = NULL;
  z_enter();

  g_return_val_if_fail((err == NULL) || (*err == NULL), G_IO_STATUS_ERROR);

  if (self->ungot_bufs)
    {
      GList *l;
      ZPktBuf *pack;

      l = self->ungot_bufs;
      pack = (ZPktBuf *) l->data;

      if (count >= pack->length)
        {
          /* consume the whole packet */
          memcpy(buf, pack->data, pack->length);
          *bytes_read = pack->length;

          self->ungot_bufs = g_list_remove_link(self->ungot_bufs, self->ungot_bufs);
          g_list_free_1(l);
          z_pktbuf_unref(pack);
        }
      else
        {
          /* consume part of the packet */
          memcpy(buf, pack->data, count);
          *bytes_read = count;

          memmove(pack->data, pack->data + count, pack->length - count);
          pack->data = static_cast<guchar *>(g_realloc(pack->data, pack->length - count));
          pack->length = pack->length - count;
        }

      res = G_IO_STATUS_NORMAL;
    }
  else
    {
      res = Z_FUNCS(self, ZStream)->read(self, buf, count, bytes_read, &local_error);
    }

  if (res == G_IO_STATUS_ERROR)
    {
      /*LOG
        This message indicates that reading from the given stream failed of
        the given reason.
       */
      z_log(self->name, CORE_ERROR, 1, "Stream read failed; stream='%s', reason='%s'", self->super.isa->name, local_error ? local_error->message : "unknown");
    }
  else if (res == G_IO_STATUS_NORMAL)
    {
      self->bytes_recvd += *bytes_read;
      z_stream_data_dump(self, G_IO_IN, buf, *bytes_read);
    }

  if (local_error)
    g_propagate_error(err, local_error);

  z_leave();
  return res;
}

/**
 * This function is called to write bytes to a stream.
 *
 * @param[in]  self ZStream instance
 * @param[in]  buf source buffer
 * @param[in]  count size of buf
 * @param[out] bytes_written number of bytes written
 * @param[out] err error value
 *
 * @returns GLib I/O status
 **/
GIOStatus
z_stream_write(ZStream *self, const void *buf, gsize count, gsize *bytes_written, GError **err)
{
  GIOStatus res;
  GError *local_error = NULL;

  g_return_val_if_fail((err == NULL) || (*err == NULL), G_IO_STATUS_ERROR);

  res = Z_FUNCS(self, ZStream)->write(self, buf, count, bytes_written, &local_error);

  if (res == G_IO_STATUS_ERROR)
    {
      /*LOG
        This message indicates that some I/O error occurred in
        the write() system call.
       */
      z_log(self->name, CORE_ERROR, 1, "Stream write failed; stream='%s', reason='%s'", self->super.isa->name, local_error ? local_error->message : "unknown");

    }
  else if (res == G_IO_STATUS_NORMAL)
    {
      self->bytes_sent += *bytes_written;
      z_stream_data_dump(self, G_IO_OUT, buf, *bytes_written);
    }

  if (local_error)
    g_propagate_error(err, local_error);
  return res;
}

/**
 * This function is called to close the stream, it also initiates
 * destructing the stream stack structure by calling
 * z_stream_struct_unref().
 *
 * @param[in]  self ZStream instance
 * @param[out] error error value
 *
 * @note this can be called from a thread concurrent to the callbacks.
 *
 * @returns GIOStatus value
 **/
static GIOStatus
z_stream_close_method(ZStream *self, GError **error)
{
  GIOStatus res;

  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  /*LOG
    This message indicates that the given stream is to be closed.
   */
  z_log((gchar *) self->name, CORE_DEBUG, 6, "Closing stream; type='%s'", self->super.isa->name);
  if (self->child)
    res = z_stream_close(self->child, error);
  else
    res = G_IO_STATUS_NORMAL;

  z_stream_struct_unref(self);

  return res;
}

/**
 * This function reads exactly len bytes into buf.
 *
 * @param[in]  self this instance
 * @param[in]  buf buffer to read data into
 * @param[in]  len size of data to read
 * @param[out] bytes_read size of data successfully read
 * @param[out] error error
 *
 * @returns G_IO_STATUS_NORMAL if successful and G_IO_STATUS_ERROR otherwise.
 *
 * @note this function may return less data than specified by its len parameter
 * when an EOF is encountered on the input stream.
 *
 * If G_IO_STATUS_ERROR is returned then the value in buf is undefined and
 * the number of characters read is also undefined.
 *
 * @returns GIOStatus value
 **/
GIOStatus
z_stream_read_chunk(ZStream *self, void *buf, gsize len, gsize *bytes_read, GError **error)
{
  GIOStatus status = G_IO_STATUS_NORMAL;
  gsize bytes;

  /* Only works on a blocking stream. */
  g_return_val_if_fail(z_stream_get_nonblock(self) == FALSE, G_IO_STATUS_ERROR);

  g_return_val_if_fail((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  z_enter();
  *bytes_read = 0;
  while (len > 0 &&
         (status == G_IO_STATUS_NORMAL || status == G_IO_STATUS_AGAIN))
    {
      status = z_stream_read(self, buf, len, &bytes, error);
      if (status == G_IO_STATUS_NORMAL)
        {
          /* FIXME: This line is really like this:
           * buf += bytes
           * just this cannot be used in windows.
           * The logic transform:
           * (gchar *)buf += bytes
           * is compile on windows, but not on linux.
           * But there are a webpage
           * http://gcc.gnu.org/onlinedocs/gcc-3.3.6/gcc/Lvalues.html
           * which say that the expression above is almost equal to the
           * expression below.
           */
          buf = (void *)(gchar *) ((gchar *)buf + bytes);
          len -= bytes;
          *bytes_read += bytes;
        }
    }
  g_assert(status != G_IO_STATUS_AGAIN);
  if (status == G_IO_STATUS_EOF && *bytes_read > 0)
    status = G_IO_STATUS_NORMAL;
  z_return(status);
}

/**
 * Attempt to write exactly len bytes to the stream by retrying the write
 * call multiple times.
 *
 * @param[in]  self this instance
 * @param[in]  buf buffer to read data into
 * @param[in]  len size of data to read
 * @param[out] bytes_written size of the data successfully written
 * @param[out] error error
 *
 * This function writes attempts to write exactly len bytes to stream by
 * retrying the write call multiple times. As EOF is not possible for
 * writing a partial write is only possible if an error occurs while writing
 * the data. The bytes_written value is only present to be symmetric with
 * z_stream_read_chunk().
 *
 * @returns GIOStatus value
 **/
GIOStatus
z_stream_write_chunk(ZStream *self, const void *buf, gsize len, gsize *bytes_written, GError **error)
{
  GIOStatus status = G_IO_STATUS_NORMAL;
  gsize bytes;

  z_enter();
  *bytes_written = 0;
  while (status == G_IO_STATUS_NORMAL && len > 0)
    {
      status = z_stream_write(self, buf, len, &bytes, error);
      if (status == G_IO_STATUS_NORMAL)
        {
          buf = (void *)(const gchar *) ((const gchar *)buf + bytes);
          len -= bytes;
          *bytes_written += bytes;
        }
    }
  g_assert(status != G_IO_STATUS_AGAIN);
  z_return(status);
}

/**
 * The default unget_packet method for streams.
 *
 * @param[in]  self ZStream instance
 * @param[in]  pack packet to unget
 * @param[out] error error value
 *
 * This is the default unget_packet method for streams. It puts the
 * pack argument to its list of ungot packets.
 *
 * @return TRUE on success (it only fails if error is a NULL pointer or points to a NULL pointer)
 **/
static gboolean
z_stream_unget_packet_method(ZStream *self, ZPktBuf *pack, GError **error)
{
  ZStream *p;

  g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);
  z_enter();
  for (p = self; p; p = p->child)
    {
      if ((p->umbrella_flags & G_IO_IN))
        {
          p->ungot_bufs = g_list_prepend(p->ungot_bufs, pack);
          break;
        }
    }
  z_return(TRUE);
}

/**
 * This function is the generic unget method which copies bytes to a
 * packet buffer and calls z_stream_unget_packet().
 *
 * @param[in]  self ZStream instance
 * @param[in]  buf data to unget
 * @param[in]  count number of bytes to buf
 * @param[out] error error value
 *
 * @returns TRUE on success
 **/
gboolean
z_stream_unget(ZStream *self, const void *buf, gsize count, GError **error)
{
  ZPktBuf *pack;

  g_return_val_if_fail((error == NULL) || (*error == NULL), FALSE);

  pack = z_pktbuf_new();
  z_pktbuf_copy(pack, buf, count);
  if (!z_stream_unget_packet(self, pack, error))
    {
      z_pktbuf_unref(pack);
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

/**
 * Constructor for ZStream and derived classes.
 *
 * @param[in] klass class of the new object, must be compatible with ZStream
 * @param[in] name name of the new object
 * @param[in] umbrella_flags umbrella flags of the new object
 *
 * @returns new object
 **/
ZStream *
z_stream_new(ZClass *klass, const gchar *name, gint umbrella_flags)
{
  ZStream *self;

  z_enter();
  self = Z_NEW_COMPAT(klass, ZStream);
  z_stream_set_name(self, name);
  self->timeout = -2;
  self->time_open = time(NULL);
  self->umbrella_state = self->umbrella_flags = umbrella_flags;
  z_refcount_set(&self->struct_ref, 1);
  z_return(self);
}

/**
 * Destroy the stream structure but don't close the fd.
 *
 * @param[in] self ZStream instance
 *
 * This function can be called instead of z_stream_close() if only the
 * stream structure needs to be destroyed without actually closing the
 * fd under the stream. It is useful when the application needs to
 * retain the fd for later use.
 **/
void
z_stream_destroy(ZStream *self)
{
  z_stream_struct_unref(self);
}

/**
 * Destructor for ZStream and derived classes.
 *
 * @param[in] s self
 **/
void
z_stream_free_method(ZObject *s)
{
  ZStream *self = Z_CAST(s, ZStream);
  time_t time_close;

  z_enter();

  g_assert(self->child == NULL);

  while (self->ungot_bufs)
    {
      GList *l;

      l = self->ungot_bufs;
      z_pktbuf_unref((ZPktBuf *) l->data);
      self->ungot_bufs = g_list_remove_link(self->ungot_bufs, self->ungot_bufs);
      g_list_free_1(l);
    }

  time_close = time(NULL);

  /*LOG
    This message contains accounting information on the given channel. It
    reports the number of seconds the fd was open and the number of
    bytes sent/received on this channel.
    */
  z_log(self->name, CORE_ACCOUNTING, 4,
        "accounting info; type='%s', duration='%d', sent='%" G_GUINT64_FORMAT "', received='%" G_GUINT64_FORMAT "'",
        s->isa->name,
        (int) difftime(time_close, self->time_open),
        self->bytes_sent,
        self->bytes_recvd);
#if ZORPLIB_ENABLE_DEBUG
  /* FIXME: Too many assert oocured
  g_assert(self->struct_ref.counter == 0);
  */
#endif

  z_stream_drop_callbacks(self);

  g_free((gpointer) self->name);

  z_object_free_method(s);
  z_return();
}

void
z_stream_set_keepalive(ZStream *self, gint keepalive)
{
  gint fd = z_stream_get_fd(self);

  keepalive = !!keepalive;
  if (fd != -1)
    {
      z_fd_set_keepalive(fd, keepalive);
      z_stream_ctrl(self, ZST_CTRL_SET_KEEPALIVE, &keepalive, sizeof(keepalive));
    }
}

void
z_stream_set_nodelay(ZStream *self, gint nodelay)
{
  if (self)
    {
      gint fd = z_stream_get_fd(self);
      nodelay = !!nodelay;
      if (fd != -1)
        z_fd_set_nodelay(fd, nodelay);
    }
}

/**
 * ZStream virtual methods.
 **/
ZStreamFuncs z_stream_funcs =
{
  {
    Z_FUNCS_COUNT(ZStream),
    z_stream_free_method,
  },
  NULL,   		/* read */
  NULL,   		/* write */
  NULL,   		/* read_pri */
  NULL,   		/* write_pri */
  NULL,   		/* shutdown */
  z_stream_close_method,/* close */
  z_stream_ctrl_method, /* ctrl */
  z_stream_attach_source_method, /* attach_source */
  z_stream_detach_source_method, /* detach_source */
  NULL,			/* watch_prepare */
  NULL,			/* watch_check */
  NULL,			/* watch_dispatch */
  NULL,			/* watch_finalize */
  z_stream_extra_get_size_method,
  z_stream_extra_save_method,
  z_stream_extra_restore_method,
  z_stream_set_child_method,
  z_stream_unget_packet_method
};

/**
 * ZStream class descriptor.
 **/
#ifdef G_OS_WIN32
  LIBZORPLL_EXTERN
#endif
Z_CLASS_DEF(ZStream, ZObject, z_stream_funcs);
