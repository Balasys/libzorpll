/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_PACKETBUF_H_INCLUDED
#define ZORP_PACKETBUF_H_INCLUDED

#include <zorpll/zorplib.h>

#ifdef __cplusplus
extern "C" {
#endif

/** glib is missing this endianness define */
#define G_HOST_ENDIAN G_BYTE_ORDER
/** glib is missing this endianness define */
#define G_NETWORK_ENDIAN G_BIG_ENDIAN

typedef enum _ZPktBufFlags
{
  Z_PB_NONE             = 0x0000,
  Z_PB_BORROWED         = 0x0001,
} ZPktBufFlags;

/**
 * Buffer intended to hold a single packet.
 **/
typedef struct _ZPktBuf
{
  ZRefCount ref_cnt;
  gsize allocated, length, pos;
  ZPktBufFlags flags;
  guchar *data;
} ZPktBuf;
    
/* Get attributes */
static inline void *z_pktbuf_data(ZPktBuf *self) { return self->data; }
static inline void *z_pktbuf_current(ZPktBuf *self) { return self->data + self->pos; }
static inline void *z_pktbuf_end(ZPktBuf *self) { return self->data + self->length; }
static inline gsize z_pktbuf_size(const ZPktBuf *self) { return self->allocated; }
static inline gsize z_pktbuf_free_size(const ZPktBuf *self) { return self->allocated - self->length; }
static inline gsize z_pktbuf_length(const ZPktBuf *self) { return self->length; }
static inline gsize z_pktbuf_pos(const ZPktBuf *self) { return self->pos; }
static inline ZPktBufFlags z_pktbuf_flags(const ZPktBuf *self) { return self->flags; }

/* Work with the buffer itself, regardless of position */
void z_pktbuf_resize(ZPktBuf *self, gsize size);
gboolean z_pktbuf_copy(ZPktBuf *self, const void *data, gsize length);
void z_pktbuf_relocate(ZPktBuf *self, void *data, gsize length, gboolean is_borrowed);
gboolean z_pktbuf_append(ZPktBuf *self, const void *data, gsize length);
gboolean z_pktbuf_insert(ZPktBuf *self, gsize pos, const void *data, gsize length);
gboolean z_pktbuf_data_equal(ZPktBuf *lhs, ZPktBuf *rhs);

/* Instance control */
ZPktBuf *z_pktbuf_new(void);
ZPktBuf *z_pktbuf_ref(ZPktBuf *self);
void z_pktbuf_unref(ZPktBuf *self);
ZPktBuf *z_pktbuf_part(ZPktBuf *self, gsize pos, gsize len);
void z_pktbuf_dump(const gchar *session_id, const gchar *class_, int level, ZPktBuf *self, const gchar *title);

/* Fancy constructors */
ZPktBuf *z_pktbuf_new_from_gstring(const GString * const str);

static inline void
z_pktbuf_data_dump(const gchar *session_id, const gchar *class_, int level, ZPktBuf *self)
{
  z_pktbuf_dump(session_id, class_, level, self, NULL);
}

/* Position-related operations */
static inline gsize z_pktbuf_available(ZPktBuf *self) { return (self->length - self->pos); }
gboolean z_pktbuf_set_available(ZPktBuf *self, gsize size);

gboolean z_pktbuf_seek(ZPktBuf *self, GSeekType whence, gssize pos);

gboolean z_pktbuf_get_boolean(ZPktBuf *self, gboolean *res);
gboolean z_pktbuf_put_boolean(ZPktBuf *self, gboolean res);
gboolean z_pktbuf_get_boolean16(ZPktBuf *self, gboolean *res);

gboolean z_pktbuf_get_u8(ZPktBuf *self, guint8 *res);
gboolean z_pktbuf_get_u16(ZPktBuf *self, gint e, guint16 *res);
gboolean z_pktbuf_get_u32(ZPktBuf *self, gint e, guint32 *res);
gboolean z_pktbuf_get_u64(ZPktBuf *self, gint e, guint64 *res);

gboolean z_pktbuf_put_u8(ZPktBuf *self, guint8 d);
gboolean z_pktbuf_put_u16(ZPktBuf *self, gint e, guint16 d);
gboolean z_pktbuf_put_u32(ZPktBuf *self, gint e, guint32 d);
gboolean z_pktbuf_put_u64(ZPktBuf *self, gint e, guint64 d);

gboolean z_pktbuf_get_u8s(ZPktBuf *self, gsize n, guint8 *res);
gboolean z_pktbuf_get_u16s(ZPktBuf *self, gint e, gsize n, guint16 *res);
gboolean z_pktbuf_get_u32s(ZPktBuf *self, gint e, gsize n, guint32 *res);
gboolean z_pktbuf_get_u64s(ZPktBuf *self, gint e, gsize n, guint64 *res);

gboolean z_pktbuf_put_u8s(ZPktBuf *self, gsize n, const guint8 *d);
gboolean z_pktbuf_put_u16s(ZPktBuf *self, gint e, gsize n, const guint16 *d);
gboolean z_pktbuf_put_u32s(ZPktBuf *self, gint e, gsize n, const guint32 *d);
gboolean z_pktbuf_put_u64s(ZPktBuf *self, gint e, gsize n, const guint64 *d);

static inline gboolean z_pktbuf_get_u16_le(ZPktBuf *self, guint16 *res) { return z_pktbuf_get_u16(self, G_LITTLE_ENDIAN, res); }
static inline gboolean z_pktbuf_get_u64_le(ZPktBuf *self, guint64 *res) { return z_pktbuf_get_u64(self, G_LITTLE_ENDIAN, res); }
static inline gboolean z_pktbuf_get_u32_le(ZPktBuf *self, guint32 *res) { return z_pktbuf_get_u32(self, G_LITTLE_ENDIAN, res); }

static inline gboolean z_pktbuf_put_u16_le(ZPktBuf *self, guint16 res) { return z_pktbuf_put_u16(self, G_LITTLE_ENDIAN, res); }
static inline gboolean z_pktbuf_put_u32_le(ZPktBuf *self, guint32 res) { return z_pktbuf_put_u32(self, G_LITTLE_ENDIAN, res); }
static inline gboolean z_pktbuf_put_u64_le(ZPktBuf *self, guint64 res) { return z_pktbuf_put_u64(self, G_LITTLE_ENDIAN, res); }

static inline gboolean z_pktbuf_get_u16_be(ZPktBuf *self, guint16 *res) { return z_pktbuf_get_u16(self, G_BIG_ENDIAN, res); }
static inline gboolean z_pktbuf_get_u32_be(ZPktBuf *self, guint32 *res) { return z_pktbuf_get_u32(self, G_BIG_ENDIAN, res); }
static inline gboolean z_pktbuf_get_u64_be(ZPktBuf *self, guint64 *res) { return z_pktbuf_get_u64(self, G_BIG_ENDIAN, res); }

static inline gboolean z_pktbuf_put_u16_be(ZPktBuf *self, guint16 res) { return z_pktbuf_put_u16(self, G_BIG_ENDIAN, res); }
static inline gboolean z_pktbuf_put_u32_be(ZPktBuf *self, guint32 res) { return z_pktbuf_put_u32(self, G_BIG_ENDIAN, res); }
static inline gboolean z_pktbuf_put_u64_be(ZPktBuf *self, guint64 res) { return z_pktbuf_put_u64(self, G_BIG_ENDIAN, res); }

static inline gboolean z_pktbuf_get_s8(ZPktBuf *self, gint8 *res)   { return z_pktbuf_get_u8(self, (guint8*)res); }
static inline gboolean z_pktbuf_get_c8(ZPktBuf *self, gchar *res)   { return z_pktbuf_get_u8(self, (guint8*)res); }
static inline gboolean z_pktbuf_get_s16(ZPktBuf *self, gint e, gint16 *res) { return z_pktbuf_get_u16(self, e, (guint16*)res); }
static inline gboolean z_pktbuf_get_s32(ZPktBuf *self, gint e, gint32 *res) { return z_pktbuf_get_u32(self, e, (guint32*)res); }
static inline gboolean z_pktbuf_get_s64(ZPktBuf *self, gint e, gint64 *res) { return z_pktbuf_get_u64(self, e, (guint64*)res); }

static inline gboolean z_pktbuf_put_s8(ZPktBuf *self, gint8 d)   { return z_pktbuf_put_u8(self, (guint8)d); }
static inline gboolean z_pktbuf_put_c8(ZPktBuf *self, gchar d)   { return z_pktbuf_put_u8(self, (guint8)d); }
static inline gboolean z_pktbuf_put_s16(ZPktBuf *self, gint e, gint16 d) { return z_pktbuf_put_u16(self, e, (guint16)d); }
static inline gboolean z_pktbuf_put_s32(ZPktBuf *self, gint e, gint32 d) { return z_pktbuf_put_u32(self, e, (guint32)d); }
static inline gboolean z_pktbuf_put_s64(ZPktBuf *self, gint e, gint64 d) { return z_pktbuf_put_u64(self, e, (guint64)d); }

static inline gboolean z_pktbuf_get_s16_le(ZPktBuf *self, gint16 *res) { return z_pktbuf_get_s16(self, G_LITTLE_ENDIAN, res); }
static inline gboolean z_pktbuf_get_s64_le(ZPktBuf *self, gint64 *res) { return z_pktbuf_get_s64(self, G_LITTLE_ENDIAN, res); }
static inline gboolean z_pktbuf_get_s32_le(ZPktBuf *self, gint32 *res) { return z_pktbuf_get_s32(self, G_LITTLE_ENDIAN, res); }

static inline gboolean z_pktbuf_put_s16_le(ZPktBuf *self, gint16 res) { return z_pktbuf_put_s16(self, G_LITTLE_ENDIAN, res); }
static inline gboolean z_pktbuf_put_s32_le(ZPktBuf *self, gint32 res) { return z_pktbuf_put_s32(self, G_LITTLE_ENDIAN, res); }
static inline gboolean z_pktbuf_put_s64_le(ZPktBuf *self, gint64 res) { return z_pktbuf_put_s64(self, G_LITTLE_ENDIAN, res); }

static inline gboolean z_pktbuf_get_s16_be(ZPktBuf *self, gint16 *res) { return z_pktbuf_get_s16(self, G_BIG_ENDIAN, res); }
static inline gboolean z_pktbuf_get_s32_be(ZPktBuf *self, gint32 *res) { return z_pktbuf_get_s32(self, G_BIG_ENDIAN, res); }
static inline gboolean z_pktbuf_get_s64_be(ZPktBuf *self, gint64 *res) { return z_pktbuf_get_s64(self, G_BIG_ENDIAN, res); }

static inline gboolean z_pktbuf_put_s16_be(ZPktBuf *self, gint16 res) { return z_pktbuf_put_s16(self, G_BIG_ENDIAN, res); }
static inline gboolean z_pktbuf_put_s32_be(ZPktBuf *self, gint32 res) { return z_pktbuf_put_s32(self, G_BIG_ENDIAN, res); }
static inline gboolean z_pktbuf_put_s64_be(ZPktBuf *self, gint64 res) { return z_pktbuf_put_s64(self, G_BIG_ENDIAN, res); }

static inline gboolean z_pktbuf_get_s8s(ZPktBuf *self, gsize n, gint8 *res)   { return z_pktbuf_get_u8s(self, n, (guint8*)res); }
static inline gboolean z_pktbuf_get_c8s(ZPktBuf *self, gsize n, gchar *res)   { return z_pktbuf_get_u8s(self, n, (guint8*)res); }
static inline gboolean z_pktbuf_get_s16s(ZPktBuf *self, gint e, gsize n, gint16 *res) { return z_pktbuf_get_u16s(self, e, n, (guint16*)res); }
static inline gboolean z_pktbuf_get_s32s(ZPktBuf *self, gint e, gsize n, gint32 *res) { return z_pktbuf_get_u32s(self, e, n, (guint32*)res); }
static inline gboolean z_pktbuf_get_s64s(ZPktBuf *self, gint e, gsize n, gint64 *res) { return z_pktbuf_get_u64s(self, e, n, (guint64*)res); }

static inline gboolean z_pktbuf_put_s8s(ZPktBuf *self, gsize n, const gint8 *d)   { return z_pktbuf_put_u8s(self, n, (const guint8*)d); }
static inline gboolean z_pktbuf_put_c8s(ZPktBuf *self, gsize n, const gchar *d)   { return z_pktbuf_put_u8s(self, n, (const guint8*)d); }
static inline gboolean z_pktbuf_put_s16s(ZPktBuf *self, gint e, gsize n, const gint16 *d) { return z_pktbuf_put_u16s(self, e, n, (const guint16*)d); }
static inline gboolean z_pktbuf_put_s32s(ZPktBuf *self, gint e, gsize n, const gint32 *d) { return z_pktbuf_put_u32s(self, e, n, (const guint32*)d); }
static inline gboolean z_pktbuf_put_s64s(ZPktBuf *self, gint e, gsize n, const gint64 *d) { return z_pktbuf_put_u64s(self, e, n, (const guint64*)d); }

gboolean z_pktbuf_put_string(ZPktBuf *self, const gchar *str);

ZPktBuf* z_pktbuf_split(ZPktBuf *self, gsize at);
ZPktBuf* z_pktbuf_append_pktbuf(ZPktBuf *self, ZPktBuf *other);

#ifdef __cplusplus
}
#endif

#endif

