/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_READLINE_H_INCLUDED
#define ZORP_READLINE_H_INCLUDED

#include <zorpll/zorplib.h>
#include <zorpll/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZRL_EOL_NL		0x00000001 /**< end-of-line is indicated by nl */
#define ZRL_EOL_CRLF		0x00000002 /**< end-of-line is indicated by crlf pair */
#define ZRL_EOL_NUL		0x00000004
#define ZRL_EOL_FATAL		0x00000008 /**< erroneous eol mark is fatal */
#define ZRL_NUL_NONFATAL        0x00000010 /**< embedded NUL character is not fatal */

#define ZRL_TRUNCATE		0x00000020 /**< truncate if line longer than buffer */
#define ZRL_SPLIT		0x00000040 /**< split line if longer than buffer */
#define ZRL_SINGLE_READ		0x00000080 /**< don't issue several read()s when fetching a line */
#define ZRL_POLL_PARTIAL	0x00000100 /**< poll for any data, not just for complete lines */
#define ZRL_RETURN_EOL          0x00000200 /**< return end-of-line as part of the data */
#define ZRL_PARTIAL_READ	ZRL_POLL_PARTIAL

#define ZST_LINE_GET_TRUNCATE     (0x01) | ZST_CTRL_LINE_OFS
#define ZST_LINE_GET_SPLIT        (0x02) | ZST_CTRL_LINE_OFS
#define ZST_LINE_GET_SINGLE_READ  (0x03) | ZST_CTRL_LINE_OFS
#define ZST_LINE_GET_POLL_PARTIAL (0x04) | ZST_CTRL_LINE_OFS
#define ZST_LINE_GET_NUL_NONFATAL (0x05) | ZST_CTRL_LINE_OFS
#define ZST_LINE_GET_RETURN_EOL   (0x06) | ZST_CTRL_LINE_OFS
#define ZST_LINE_GET_PARTIAL_READ ZST_LINE_GET_POLL_PARTIAL

#define ZST_LINE_SET_TRUNCATE     (0x11) | ZST_CTRL_LINE_OFS
#define ZST_LINE_SET_SPLIT        (0x12) | ZST_CTRL_LINE_OFS
#define ZST_LINE_SET_SINGLE_READ  (0x13) | ZST_CTRL_LINE_OFS
#define ZST_LINE_SET_POLL_PARTIAL (0x14) | ZST_CTRL_LINE_OFS
#define ZST_LINE_SET_NUL_NONFATAL (0x15) | ZST_CTRL_LINE_OFS
#define ZST_LINE_SET_RETURN_EOL   (0x16) | ZST_CTRL_LINE_OFS
#define ZST_LINE_SET_PARTIAL_READ ZST_LINE_SET_POLL_PARTIAL

LIBZORPLL_EXTERN ZClass ZStreamLine__class;

GIOStatus z_stream_line_get(ZStream *s, gchar **line, gsize *length, GError **error);
GIOStatus z_stream_line_get_copy(ZStream *s, gchar *line, gsize *length, GError **error);
void z_stream_line_unget_line(ZStream *stream);
gboolean z_stream_line_unget(ZStream *stream, const gchar *unget_line, gsize unget_len);

ZStream *z_stream_line_new(ZStream *from, gsize bufsize, guint flags);

static inline void
z_stream_line_set_poll_partial(ZStream *stream, gboolean enable)
{
  z_stream_ctrl(stream, ZST_LINE_SET_PARTIAL_READ, &enable, sizeof(enable));
}

static inline void
z_stream_line_set_split(ZStream *stream, gboolean enable)
{
  z_stream_ctrl(stream, ZST_LINE_SET_SPLIT, &enable, sizeof(enable));
}

static inline void
z_stream_line_set_truncate(ZStream *stream, gboolean enable)
{
  z_stream_ctrl(stream, ZST_LINE_SET_TRUNCATE, &enable, sizeof(enable));
}

static inline void
z_stream_line_set_nul_nonfatal(ZStream *stream, gboolean enable)
{
  z_stream_ctrl(stream, ZST_LINE_SET_NUL_NONFATAL, &enable, sizeof(enable));
}

#ifdef __cplusplus
}
#endif

#endif
