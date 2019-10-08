#include <zorpll/streamtee.h>

#include <sys/socket.h>

/**
 * ZStream derived class that writes a copy of all data read from or written to
 * (depending on tee_direction) its child to another stream.
 **/
typedef struct _ZStreamTee
{
  ZStream super;
  ZStream *fork;        /**< stream that gets the duplicate data */

  /**
   * G_IO_IN if read information should be duplicated,
   * G_IO_OUT if written information should be duplicated.
   **/
  GIOCondition tee_direction; 
} ZStreamTee;

extern ZClass ZStreamTee__class;

/**
 * This function writes the data to the forked stream.
 *
 * @param[in] self this instance
 * @param[in] buf buffer to write
 * @param[in] count size of buf
 * @param[in] error error value
 *
 * @returns GIOStatus instance
 **/
static GIOStatus
z_stream_tee_write_fork(ZStreamTee *self, const gchar *buf, gsize count, GError **error)
{
  GIOStatus st = G_IO_STATUS_NORMAL;
  gsize bw, left;
      
  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  left = count;
  while (left)
    {
      st = z_stream_write(self->fork, buf + count - left, left, &bw, error);
      if (st != G_IO_STATUS_NORMAL)
        break;
      left -= bw;
    }
  z_return(st);
}

/**
 * The virtual read() method for ZStreamTee, it reads data from self->child
 * and writes the information read to the forked stream if the tee_direction
 * is G_IO_IN (e.g.\ the read information should be stored in self->fork)
 *
 * @param[in]  s this instance
 * @param[in]  buf buffer to read data into
 * @param[in]  count size of buf
 * @param[out] bytes_read bytes read from the stream
 * @param[out] error error value
 *
 * @returns GIOStatus instance
 **/
static GIOStatus
z_stream_tee_read_method(ZStream *s, void *buf, gsize count, gsize *bytes_read, GError **error)
{
  ZStreamTee *self = Z_CAST(s, ZStreamTee);
  GIOStatus res;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  self->super.child->timeout = self->super.timeout;
  res = z_stream_read(self->super.child, buf, count, bytes_read, error);
  if (res == G_IO_STATUS_NORMAL && self->tee_direction == G_IO_IN && *bytes_read)
    {
      res = z_stream_tee_write_fork(self, static_cast<const gchar *>(buf), *bytes_read, error);
    }
  z_return(res);
}

/**
 * The virtual write() method for ZStreamTee, it writes data to self->child
 * and writes the information written to the forked stream if the tee_direction
 * is G_IO_OUT (e.g.\ the written information should be stored in self->fork)
 *
 * @param[in]  s this instance
 * @param[in]  buf buffer to write
 * @param[in]  count size of buf
 * @param[out] bytes_written bytes read from the stream
 * @param[out] error error value
 *
 * @returns GIOStatus instance
 **/
static GIOStatus
z_stream_tee_write_method(ZStream *s, const void *buf, gsize count, gsize *bytes_written, GError **error)
{
  ZStreamTee *self = Z_CAST(s, ZStreamTee);
  GIOStatus res;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  self->super.child->timeout = self->super.timeout;
  res = z_stream_write(self->super.child, buf, count, bytes_written, error);
  if (res == G_IO_STATUS_NORMAL && self->tee_direction == G_IO_OUT && *bytes_written)
    res = z_stream_tee_write_fork(self, static_cast<const gchar *>(buf), *bytes_written, error);

  z_return(res);
}

/**
 * The virtual shutdown() method for ZStreamTee, it shuts down the child and
 * the forked stream if the appropriate shutdown mode is specified for the
 * tee.
 *
 * @param[in]  s this instance
 * @param[in]  shutdown_mode shutdown mode (one of SHUT_* macros)
 * @param[out] error error value
 * 
 * @returns GIOStatus instance
 **/
static GIOStatus
z_stream_tee_shutdown_method(ZStream *s, gint shutdown_mode, GError **error)
{
  ZStreamTee *self = Z_CAST(s, ZStreamTee);
  GIOStatus res;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  if (shutdown_mode == SHUT_RDWR ||
      (shutdown_mode == SHUT_WR && self->tee_direction == G_IO_OUT) ||
      (shutdown_mode == SHUT_RD && self->tee_direction == G_IO_IN))
    res = z_stream_shutdown(self->fork, SHUT_RDWR, error);
  else
    res = G_IO_STATUS_NORMAL;
    
  if (res == G_IO_STATUS_NORMAL)
    res = z_stream_shutdown(s->child, shutdown_mode, error);
  z_return(res);
}

/**
 * The virtual close() method for ZStreamTee, it closes the child and the
 * forked stream.
 *
 * @param[in]  s this instance
 * @param[out] error error value
 * 
 * @returns GIOStatus instance
 **/
static GIOStatus
z_stream_tee_close_method(ZStream *s, GError **error)
{
  ZStreamTee *self = Z_CAST(s, ZStreamTee);
  GIOStatus res;

  z_enter();
  g_return_val_if_fail ((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  if ((res = z_stream_close(self->fork, error)) == G_IO_STATUS_NORMAL)
    res = Z_SUPER(s, ZStream)->close(s, error);
  z_return(res);
}


/**
 * Process stream control calls on ZStreamTee object.
 *
 * @param[in]      s ZStream instance (supposed to be ZStreamTee, but it can be any ZStream in this case)
 * @param[in]      function function selector
 * @param[in, out] value parameter to function
 * @param[in]      vlen length of value
 *
 * @returns TRUE on success
 **/
static gboolean
z_stream_tee_ctrl_method(ZStream *s, guint function, gpointer value, guint vlen)
{
  gboolean ret = FALSE;

  z_enter();
  switch (ZST_CTRL_MSG(function))
    {
    case ZST_CTRL_SET_CALLBACK_READ:
    case ZST_CTRL_SET_CALLBACK_WRITE:
    case ZST_CTRL_SET_CALLBACK_PRI:
      ret = z_stream_ctrl_method(s, function, value, vlen);
      break;

    default:
      ret = z_stream_ctrl_method(s, ZST_CTRL_MSG_FORWARD | function, value, vlen);
      break;
    }
  z_return(ret);
}

/**
 * This function is called to prepare for a poll iteration.
 *
 * @param[in]  s ZStream instance
 * @param      src GSource associated with s (unused)
 * @param[out] timeout timeout value for this poll iteration
 *
 * It checks our internal state and prepares the watch conditions for s->child.
 **/
static gboolean 
z_stream_tee_watch_prepare(ZStream *s, GSource */* src */, gint *timeout)
{
  ZStreamTee *self = Z_CAST(s, ZStreamTee);

  z_enter();
  *timeout = -1;
  z_stream_set_cond(s->child, G_IO_IN, self->super.want_read);
  z_stream_set_cond(s->child, G_IO_OUT, self->super.want_write);
  z_stream_set_cond(s->child, G_IO_PRI, self->super.want_pri);
  z_return(FALSE);
}

/**
 * This function is called to check the results of a poll iteration.
 *
 * @param s ZStream instance (unused)
 * @param src GSource associated with s (unused)
 *
 * In this case it basically does nothing and returns FALSE to indicate that
 * dispatch should not be called.
 *
 * @returns always FALSE
 **/
static gboolean 
z_stream_tee_watch_check(ZStream */* s */, GSource * /* src */)
{
  return FALSE;
}

/**
 * This function is call callbacks after check returned TRUE, which is never
 * in our case.
 *
 * @param s ZStream instance (unused)
 * @param src GSource associated with s (unused)
 *
 * It does nothing but returns TRUE.
 *
 * @returns always TRUE
 **/
static gboolean 
z_stream_tee_watch_dispatch(ZStream * /* s */, GSource * /* src */)
{
  return TRUE;
}

/**
 * This function is called whenever our child stream indicates readability. 
 *
 * @param     s child ZStream instance (unused)
 * @param[in] cond condition that triggered this callback
 * @param[in] user_data ZStream instance as a gpointer
 *
 * It basically calls our own callback as we rely on our child to trigger read/write/pri
 * callbacks.
 **/
static gboolean
z_stream_tee_read_callback(ZStream */* s */, GIOCondition cond, gpointer user_data)
{
  ZStreamTee *self = Z_CAST(user_data, ZStreamTee);

  return self->super.read_cb(&self->super, cond, self->super.user_data_read);
}

/**
 * This function is called whenever our child stream indicates writability. 
 *
 * @param     s child ZStream instance (unused)
 * @param[in] cond condition that triggered this callback
 * @param[in] user_data ZStream instance as a gpointer
 *
 * It basically calls our own callback as we rely on our child to trigger read/write/pri
 * callbacks.
 **/
static gboolean
z_stream_tee_write_callback(ZStream */* s */, GIOCondition cond, gpointer user_data)
{
  ZStreamTee *self = Z_CAST(user_data, ZStreamTee);

  return self->super.write_cb(&self->super, cond, self->super.user_data_write);
}

/**
 * This function is called whenever our child stream indicates that priority
 * data is available.
 *
 * @param     s child ZStream instance (unused)
 * @param[in] cond condition that triggered this callback
 * @param[in] user_data ZStream instance as a gpointer
 *
 * It basically calls our own callback as we rely on our
 * child to trigger read/write/pri callbacks.
 **/
static gboolean
z_stream_tee_pri_callback(ZStream */* s */, GIOCondition cond, gpointer user_data)
{
  ZStreamTee *self = Z_CAST(user_data, ZStreamTee);

  return self->super.pri_cb(&self->super, cond, self->super.user_data_pri);
}

/**
 * This function is called to change our child stream.
 *
 * @param[in] s ZStream instance
 * @param[in] new_child new child stream
 *
 * It basically sets up appropriate callbacks and switches new_child to nonblocking mode.
 **/
static void
z_stream_tee_set_child(ZStream *s, ZStream *new_child)
{
  z_stream_ref(s);

  Z_SUPER(s, ZStream)->set_child(s, new_child);

  if (new_child)
    {
      z_stream_set_callback(new_child, G_IO_IN, z_stream_tee_read_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(new_child, G_IO_OUT, z_stream_tee_write_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
      z_stream_set_callback(new_child, G_IO_PRI, z_stream_tee_pri_callback, z_stream_ref(s), (GDestroyNotify) z_stream_unref);
    }

  z_stream_unref(s);
}

/**
 * This function constructs a new ZStreamTee instance which writes data
 * flown in one direction of a stream (read or write direction) and writes
 * this data in another stream (the fork).
 *
 * @param[in] child child stream
 * @param[in] fork fork child
 * @param[in] tee_direction which direction to save 
 **/
ZStream *
z_stream_tee_new(ZStream *child, ZStream *fork, GIOCondition tee_direction)
{
  ZStreamTee *self;
  
  z_enter();
  self = Z_CAST(z_stream_new(Z_CLASS(ZStreamTee), child ? child->name : "", 0), ZStreamTee);
  self->fork = fork;
  self->tee_direction = tee_direction;
  z_stream_set_child(&self->super, child);
  z_return((ZStream *) self);

}

/**
 * Destructor of a ZStreamTee.
 *
 * @param[in] s this instance
 **/
static void
z_stream_tee_free_method(ZObject *s)
{
  ZStreamTee *self = Z_CAST(s, ZStreamTee);

  z_stream_unref(self->fork);
  z_stream_free_method(s);
}

/**
 * ZStreamTee virtual methods.
 **/
static ZStreamFuncs
z_stream_tee_funcs =
{
  {
    Z_FUNCS_COUNT(ZStream),
    z_stream_tee_free_method
  },
  z_stream_tee_read_method,
  z_stream_tee_write_method,
  NULL,
  NULL,
  z_stream_tee_shutdown_method,
  z_stream_tee_close_method,
  z_stream_tee_ctrl_method,

  NULL,
  NULL,
  z_stream_tee_watch_prepare,
  z_stream_tee_watch_check,
  z_stream_tee_watch_dispatch,
  NULL,

  NULL,
  NULL,
  NULL,
  z_stream_tee_set_child,
  NULL
};

/**
 * ZStreamTee class descriptor.
 **/
Z_CLASS_DEF(ZStreamTee, ZStream, z_stream_tee_funcs);
