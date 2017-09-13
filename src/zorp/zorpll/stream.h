/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_STREAM_H_INCLUDED
#define ZORP_STREAM_H_INCLUDED

#include <zorpll/zorplib.h>
#include <zorpll/zobject.h>
#include <zorpll/log.h>
#include <zorpll/packetbuf.h>

#include <time.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef G_OS_WIN32
  #define SHUT_RDWR SD_BOTH
  #define SHUT_WR SD_SEND
  #define SHUT_RD SD_RECEIVE
#endif

#define Z_STREAM_MAX_NAME   128

/* Stream types */

/**
 * Stream control messages are the same as ioctl() is for UNIX fds, they
 * define a generic interface for various stream implementation specific
 * operations.
 *
 *  0. byte message
 *  1. byte stream type
 *  3. byte undefined
 *  4. byte stream flags
 **/
#define ZST_CTRL_MSG(x)       ((x)&0xFFFF)
#define ZST_CTRL_MSG_FLAGS(x) ((x) & 0xFF000000)

/* stream flags */
enum
{
  ZST_CTRL_MSG_FORWARD          = 0x80000000,
};

/* message */
enum
{
  ZST_CTRL_GET_FD               = 0x01,
  ZST_CTRL_SET_COND_READ        = 0x02,
  ZST_CTRL_SET_COND_WRITE       = 0x03,
  ZST_CTRL_SET_COND_PRI         = 0x04,
  ZST_CTRL_SET_CALLBACK_READ    = 0x06,
  ZST_CTRL_SET_CALLBACK_WRITE   = 0x07,
  ZST_CTRL_SET_CALLBACK_PRI     = 0x08,
  ZST_CTRL_SET_TIMEOUT_BLOCK    = 0x0A,
  ZST_CTRL_GET_COND_READ        = 0x0C,
  ZST_CTRL_GET_COND_WRITE       = 0x0D,
  ZST_CTRL_GET_COND_PRI         = 0x0E,
  ZST_CTRL_GET_CALLBACK_READ    = 0x10,
  ZST_CTRL_GET_CALLBACK_WRITE   = 0x11,
  ZST_CTRL_GET_CALLBACK_PRI     = 0x12,
  ZST_CTRL_SET_NONBLOCK         = 0x14,
  ZST_CTRL_GET_NONBLOCK         = 0x15,
  ZST_CTRL_GET_BROKEN           = 0x16,
  ZST_CTRL_SET_CLOSEONEXEC      = 0x17,
  ZST_CTRL_GET_KEEPALIVE        = 0x18,
  ZST_CTRL_SET_KEEPALIVE        = 0x19,
  ZST_CTRL_GET_BUFFERED_BYTES   = 0x20,
};

/* stream type */
enum
{
  ZST_CTRL_LINE_OFS             = ('L' << 8),
  ZST_CTRL_SSL_OFS              = ('S' << 8),
};

typedef struct _ZStream ZStream;
typedef struct _ZStreamContext ZStreamContext;
typedef struct _ZStreamSource ZStreamSource;

typedef gboolean (*ZStreamCallback)(struct _ZStream *stream, GIOCondition cond, gpointer user_data);

GSource *z_stream_source_new(ZStream *stream);

/**
 * Stream context describing I/O callbacks to make it easy to change how a
 * stream is being handled by different parts of Zorp (ie.\ proxy and associated
 * transfer code)
 **/
struct _ZStreamContext
{
  gboolean restored;

  gboolean want_read;       /**< do we want read callbacks? */
  gpointer user_data_read;  /**< opaque pointer, can be used by read callback */
  GDestroyNotify user_data_read_notify;
  ZStreamCallback read_cb;  /**< pointer to read callback */

  gboolean want_pri;        /**< do we want urgent data callbacks? */
  gpointer user_data_pri;   /**< opaque pointer, can be used by pri callback */
  GDestroyNotify user_data_pri_notify;
  ZStreamCallback pri_cb;   /**< pointer to urgent data callback */

  gboolean want_write;      /**< do we want write callbacks */
  gpointer user_data_write; /**< opaque pointer, can be used by write callback */
  GDestroyNotify user_data_write_notify;
  ZStreamCallback write_cb; /**< pointer to write callback */

  gint timeout;
  gboolean nonblocking;

  gpointer stream_extra;
};

void z_stream_context_destroy(ZStreamContext *self);

/**
 * ZStream virtual method table type.
 **/
typedef struct _ZStreamFuncs
{
  ZObjectFuncs super;
  GIOStatus (*read)(ZStream *stream, void *buf, gsize count, gsize *bytes_read, GError **err);
  GIOStatus (*write)(ZStream *stream, const void *buf, gsize count, gsize *bytes_written, GError **err);
  GIOStatus (*read_pri)(ZStream *stream, void *buf, gsize count, gsize *bytes_read, GError **err);
  GIOStatus (*write_pri)(ZStream *stream, const void *buf, gsize count, gsize *bytes_written, GError **err);
  GIOStatus (*shutdown)(ZStream *stream, int i, GError  **err);
  GIOStatus (*close) (ZStream *stream, GError **err);
  gboolean (*ctrl) (ZStream *stream, guint function, gpointer value, guint vlen);

  void (*attach_source)(ZStream *stream, GMainContext *context);
  void (*detach_source)(ZStream *stream);

  gboolean (*watch_prepare)(ZStream *s, GSource *src, gint *timeout);
  gboolean (*watch_check)(ZStream *s, GSource *src);
  gboolean (*watch_dispatch)(ZStream *s, GSource *src);
  void (*watch_finalize)(ZStream *s, GSource *source);

  gsize (*extra_get_size)(ZStream *s);
  gsize (*extra_save)(ZStream *s, gpointer context);
  gsize (*extra_restore)(ZStream *s, gpointer context);

  void (*set_child)(ZStream *s, ZStream *new_child);
  gboolean (*unget_packet)(ZStream *s, ZPktBuf *packet, GError **error);
} ZStreamFuncs;

LIBZORPLL_EXTERN ZClass ZStream__class;

/**
 * ZStream encapsulates a generic I/O stream. It contains some attributes
 * and callbacks which are used by ZPoll. Description of some of the notions
 * used in ZStream.
 *
 * Umbrella:
 *   Being an umbrella for read or write direction means that all streams
 *   below are completely hidden from the user of the stream. For example:
 *
 * <pre>
 *    (top)      ZStreamBuf <-> ZStreamLine <-> ZStreamSsl <-> ZStreamLine <-> ZStreamFd
 *    (umbrella)    (w)              (r)           (rw)             (r)           (rw)
 * </pre>
 *
 *   In the example above the umbrella in write direction is the on-top
 *   ZStreamBuf while the umbrella in read direction is ZStreamLine. If an
 *   operation that applies to ZStreamLine instance is invoked on the top of
 *   the stream then the operation will succeed.
 *
 *   On the other hand neither read nor write operations will succeed on
 *   ZStreamSsl as it is already hidden from the top in both read and write
 *   directions.
 *
 *   Umbrellas are also used to determine whether the core routines will log
 *   the I/O dump and to implement z_stream_unget as it always stores ungot
 *   data at the read-side top.
 *
 **/
struct _ZStream
{
  ZObject super;
  const gchar *name;  /* const */

  /** current umbrella state in the current stack */
  gint umbrella_state;
  /** umbrella flags requested by the stream */
  gint umbrella_flags;
  GList *ungot_bufs;

  gint timeout;

  /* stream structure pointers */
  ZRefCount struct_ref;         /**< stream structure pointers */
  ZStream *parent;              /**< stream structure pointers */
  ZStream *child;               /**< stream structure pointers */
  gint stack_depth;             /**< stream structure pointers */
  GSource *source;              /**< stream structure pointers */

  time_t time_open;
  guint64 bytes_recvd, bytes_sent;      /**< bytes received/sent counters for accounting info logging */

  gpointer user_data_read;  /**< opaque pointer, can be used by read callback */
  GDestroyNotify user_data_read_notify;
  ZStreamCallback read_cb;  /**< pointer to read callback */
  gboolean want_read;       /**< do we want read callbacks? */

  gboolean want_pri;        /**< do we want urgent data callbacks? */
  gpointer user_data_pri;   /**< opaque pointer, can be used by pri callback */
  GDestroyNotify user_data_pri_notify;
  ZStreamCallback pri_cb;   /**< pointer to urgent data callback */

  gpointer user_data_write; /**< opaque pointer, can be used by write callback */
  GDestroyNotify user_data_write_notify;
  ZStreamCallback write_cb; /**< pointer to write callback */
  gboolean want_write;      /**< do we want write callbacks */
};

ZStream *z_stream_new(ZClass *class_, const gchar *name, gint umbrella_flags);
gboolean z_stream_save_context(ZStream *self, ZStreamContext *context);
gboolean z_stream_restore_context(ZStream *self, ZStreamContext *context);
GIOStatus z_stream_read(ZStream *self, void *buf, gsize count, gsize *bytes_read, GError **err);
GIOStatus z_stream_write(ZStream *self, const void *buf, gsize count, gsize *bytes_written, GError **err);
gboolean z_stream_set_cond(ZStream *s, guint type, gboolean value);
gboolean z_stream_get_cond(ZStream *s, guint type, gboolean *value);
gboolean z_stream_set_callback(ZStream *s, guint type, ZStreamCallback callback, gpointer user_data, GDestroyNotify notify);
ZStream *z_stream_push(ZStream *self, ZStream *new_top);
ZStream *z_stream_pop(ZStream *self);
gboolean z_stream_unget(ZStream *self, const void *buf, gsize count, GError **error);
void z_stream_destroy(ZStream *self);

/* virtual methods for static references like calling the superclass's function in derived classes */

gboolean z_stream_ctrl_method(ZStream *s, guint function, gpointer value, guint vlen);
void z_stream_free_method(ZObject *s);

/**
 * Reference a ZStream instance.
 *
 * @param[in] self ZStream instance
 *
 * Increments the reference count of self and returns a reference to it.
 *
 * @returns self
 **/
static inline ZStream *
z_stream_ref(ZStream *self)
{
  return (ZStream *) z_object_ref(&self->super);
}

/**
 * Unreference a ZStream instance.
 *
 * @param[in] self ZStream instance
 *
 * Decrements the reference count of self.
 **/
static inline void
z_stream_unref(ZStream *self)
{
  z_object_unref(&self->super);
}

/**
 * Set the name of a stream (and its child, if any)
 *
 * @param[in] self ZStream instance
 * @param[in] new_name new name to set, it will be g_strdup-ed
 **/
static inline void
z_stream_set_name(ZStream *self, const gchar *new_name)
{
  g_return_if_fail(new_name);

  if (self->name)
    g_free((gpointer) self->name);
  if (new_name)
    self->name = g_strdup(new_name);
  if (self->child)
    z_stream_set_name(self->child, new_name);
}

static inline void
z_stream_data_dump(ZStream *self, gint direction, const void *data, gsize data_len)
{
  if (self->umbrella_state & direction)
    {
      /*LOG
        This message reports the number of bytes read from the given fd.
       */
      if (direction == G_IO_IN)
        z_log(self->name, CORE_DUMP, 7, "Reading stream; stream='%s', count='%zd'", self->super.isa->name, data_len);
      else
        z_log(self->name, CORE_DUMP, 7, "Writing stream; stream='%s', count='%zd'", self->super.isa->name, data_len);

      z_log_data_dump(self->name, CORE_DUMP, 9, data, data_len);
    }

}

/* virtual functions */

/**
 * Call ctrl virtual method.
 *
 * @see Default definition: z_stream_ctrl_method()
 **/
static inline gboolean
z_stream_ctrl(ZStream *self, guint function, gpointer value, guint vlen)
{
  return Z_FUNCS(self, ZStream)->ctrl(self, function, value, vlen);
}

/**
 * Call read_pri virtual method.
 *
 * Method is NULL by default.
 **/
static inline GIOStatus
z_stream_read_pri(ZStream *self, void *buf, gsize count, gsize *bytes_read, GError **err)
{
  return Z_FUNCS(self, ZStream)->read_pri(self, buf, count, bytes_read, err);
}

/**
 * Call write_pri virtual method.
 *
 * Method is NULL by default.
 **/
static inline GIOStatus
z_stream_write_pri(ZStream *self, const void *buf, gsize count, gsize *bytes_written, GError **err)
{
  return Z_FUNCS(self, ZStream)->write_pri(self, buf, count, bytes_written, err);
}

/**
 * Call shutdown(2) for the ZStream if applicable.
 *
 * @param[in]  stream ZStreamFD instance
 * @param[in]  how HOW argument to shutdown
 * @param[out] err error value
 *
 * The shutdown method will be called on the stream if it has one.
 * That method will call shutdown(2) on the fd.
 *
 * The action to perform is specified by i as follows:
 * - i == 0: Stop receiving data.
 * - i == 1: Stop trying to transmit data.
 * - i == 2: Stop both reception and transmission.
 *
 * @returns GIOStatus value
 **/
static inline GIOStatus
z_stream_shutdown(ZStream *self, int how, GError **err)
{
  if (Z_FUNCS(self, ZStream)->shutdown)
    return Z_FUNCS(self, ZStream)->shutdown(self, how, err);
  return G_IO_STATUS_NORMAL;
}

/**
 * Call close virtual method.
 *
 * @see Default definition: z_stream_close_method()
 **/
static inline GIOStatus
z_stream_close(ZStream *self, GError **err)
{
  return Z_FUNCS(self, ZStream)->close(self, err);
}

/**
 * Call attach_source virtual method.
 *
 * @see Default definition: z_stream_attach_source_method()
 **/
static inline void
z_stream_attach_source(ZStream *self, GMainContext *context)
{
  Z_FUNCS(self, ZStream)->attach_source(self, context);
}

/**
 * Call detach_source virtual method.
 *
 * @see Default definition: z_stream_detach_source_method()
 **/
static inline void
z_stream_detach_source(ZStream *self)
{
  Z_FUNCS(self, ZStream)->detach_source(self);
}

/**
 * Call watch_prepare virtual method.
 *
 * Method is NULL by default.
 **/
static inline gboolean
z_stream_watch_prepare(ZStream *self, GSource *s, gint *timeout)
{
  return Z_FUNCS(self, ZStream)->watch_prepare(self, s, timeout);
}

/**
 * Call watch_check virtual method.
 *
 * Method is NULL by default.
 **/
static inline gboolean
z_stream_watch_check(ZStream *self, GSource *s)
{
  return Z_FUNCS(self, ZStream)->watch_check(self, s);
}

/**
 * Call watch_dispatch virtual method.
 *
 * Method is NULL by default.
 **/
static inline gboolean
z_stream_watch_dispatch(ZStream *self, GSource *s)
{
  return Z_FUNCS(self, ZStream)->watch_dispatch(self, s);
}

/**
 * Call watch_finalize virtual method.
 *
 * Method is NULL by default.
 **/
static inline void
z_stream_watch_finalize(ZStream *self, GSource *s)
{
  if (Z_FUNCS(self, ZStream)->watch_finalize)
    Z_FUNCS(self, ZStream)->watch_finalize(self, s);
}

/**
 * Call extra_get_size virtual method.
 *
 * @see Default definition: z_stream_extra_get_size_method()
 **/
static inline gsize
z_stream_extra_get_size(ZStream *s)
{
  return Z_FUNCS(s, ZStream)->extra_get_size(s);
}

/**
 * Call extra_save virtual method.
 *
 * @see Default definition: z_stream_extra_save_method()
 **/
static inline gsize
z_stream_extra_save(ZStream *s, gpointer context)
{
  return Z_FUNCS(s, ZStream)->extra_save(s, context);
}

/**
 * Call extra_restore virtual method.
 *
 * @see Default definition: z_stream_extra_restore_method()
 **/
static inline gsize
z_stream_extra_restore(ZStream *s, gpointer context)
{
  return Z_FUNCS(s, ZStream)->extra_restore(s, context);
}

/**
 * Call set_child virtual method.
 *
 * @see Default definition: z_stream_set_child_method()
 **/
static inline void
z_stream_set_child(ZStream *s, ZStream *new_child)
{
  Z_FUNCS(s, ZStream)->set_child(s, new_child);
}

/**
 * Call unget_packet virtual method.
 *
 * @see Default definition: z_stream_unget_packet_method()
 **/
static inline gboolean
z_stream_unget_packet(ZStream *s, ZPktBuf *pack, GError **error)
{
  return Z_FUNCS(s, ZStream)->unget_packet(s, pack, error);
}

/* helper functions */

/**
 * Get the fd associated with the stream, if any.
 *
 * @param[in] s ZStream instance
 *
 * @returns -1 if there's no fd or the actual fd if there is.
 **/
static inline int
z_stream_get_fd(ZStream *s)
{
  gint ret = -1;
  if (!z_stream_ctrl(s, ZST_CTRL_GET_FD, &ret, sizeof(ret)))
    ret = -1;
  return ret;
}

/**
 * Check if the connection associated with the stream (if any) is broken.
 *
 * @param[in] s ZStream instance
 *
 * @returns broken state if it could be determined; FALSE otherwise.
 **/
static inline gboolean
z_stream_broken(ZStream *s)
{
  gboolean ret = -1;
  if (!z_stream_ctrl(s, ZST_CTRL_GET_BROKEN, &ret, sizeof(ret)))
    ret = FALSE;
  return ret;
}

/**
 * Set timeout of stream.
 *
 * @param[in] s ZStream instance
 * @param[in] timeout new timeout value
 *
 * Emits a SET_TIMEOUT_BLOCK control message that propagates down through the
 * stream stack.
 *
 * @returns TRUE if setting the timeout succeeded
 **/
static inline gboolean
z_stream_set_timeout(ZStream *s, gint timeout)
{
  gboolean ret = TRUE;

  if (!z_stream_ctrl(s, ZST_CTRL_MSG_FORWARD | ZST_CTRL_SET_TIMEOUT_BLOCK,
                     &timeout, sizeof(timeout)))
    ret = FALSE;

  return ret;
}

/**
 * Set nonblock of stream.
 *
 * @param[in] self ZStream instance
 * @param[in] nonblock new value
 *
 * @todo FIXME-DOC: write a better description. also, write a description for the ones below.
 **/
static inline gboolean
z_stream_set_nonblock(ZStream *self, gboolean nonblock)
{
  return z_stream_ctrl(self, ZST_CTRL_SET_NONBLOCK, &nonblock, sizeof(nonblock));
}

static inline gboolean
z_stream_get_nonblock(ZStream *self)
{
  gboolean nonblock;

  z_stream_ctrl(self, ZST_CTRL_GET_NONBLOCK, &nonblock, sizeof(nonblock));
  return nonblock;
}

static inline gboolean
z_stream_set_closeonexec(ZStream *self, gboolean cloexec)
{
  return z_stream_ctrl(self, ZST_CTRL_SET_CLOSEONEXEC, &cloexec, sizeof(cloexec));
}

static inline gint
z_stream_get_keepalive(ZStream *self)
{
  gint keepalive;
  z_stream_ctrl(self, ZST_CTRL_GET_KEEPALIVE, &keepalive, sizeof(keepalive));
  return keepalive;
}

void z_stream_set_keepalive(ZStream *self, gint keepalive);

void z_stream_set_nodelay(ZStream *self, gint nodelay);

static inline gsize
z_stream_get_buffered_bytes(ZStream *self)
{
  gsize bytes = 0;

  if (z_stream_ctrl(self, ZST_CTRL_GET_BUFFERED_BYTES, &bytes, sizeof(bytes)))
    return bytes;
  else
    return G_MAXSIZE;
}

ZStream *
z_stream_search_stack(ZStream *top, gint direction, ZClass *class_);

GIOStatus z_stream_read_chunk(ZStream *self, void *buf, gsize count, gsize *bytes_read, GError **err);
GIOStatus z_stream_write_chunk(ZStream *self, const void *buf, gsize count, gsize *bytes_written, GError **err);

#ifdef __cplusplus
}
#endif

#endif
