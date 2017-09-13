/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_STREAMGZIP_H_INCLUDED
#define ZORP_STREAMGZIP_H_INCLUDED

#include <zorpll/stream.h>
#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* gzip flags */
enum
{
  Z_SGZ_SYNC_FLUSH         = 0x0001,     /**< flush output for every write */
  Z_SGZ_GZIP_HEADER        = 0x0002,     /**< read/write a gzip file header, requires a blocking child */
  Z_SGZ_WRITE_EMPTY_HEADER = 0x0004,     /**< write the gzip header even if nothing was written */
};

#define GZIP_MAGIC_LEN    4
#define GZIP_MAGIC_1      0x1F
#define GZIP_MAGIC_2      0x8B
#define GZIP_IS_GZIP_MAGIC(magic_buf)  (((magic_buf)[0] == GZIP_MAGIC_1) && ((magic_buf)[1] == GZIP_MAGIC_2) && ((magic_buf)[2] == Z_DEFLATED) && !((magic_buf)[3] & 0xe0))

gboolean z_stream_gzip_fetch_header(ZStream *s, GError **error);
void z_stream_gzip_get_header_fields(ZStream *s, time_t *timestamp, gchar **origname, gchar **comment, gint *extra_len, gchar **extra);
void z_stream_gzip_set_header_fields(ZStream *s, time_t timestamp, const gchar *origname, const gchar *comment, gint extra_len, const gchar *extra);

ZStream *z_stream_gzip_new(ZStream *child, gint flags, guint level, guint buffer_length);

#ifdef __cplusplus
}
#endif

#endif /* ZORP_STREAMGZIP_H_INCLUDED */
