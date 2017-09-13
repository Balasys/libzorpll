/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_STREAMBUF_H_INCLUDED
#define ZORP_STREAMBUF_H_INCLUDED

#include <zorpll/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
  /**
   * Whether flush is attempted immediately after a write operation. Should
   * only be used when the flush process and write calls are in the same
   * thread, as the child stream might not support multiple threads entering
   * their write method at the same time. Otherwise it is worth using as it
   * might improve write latency a lot (=no need to wait for a poll() loop
   * to check for writability)
   **/
  Z_SBF_IMMED_FLUSH=0x0001 
};

gboolean z_stream_buf_space_avail(ZStream *s);
GIOStatus z_stream_write_buf(ZStream *stream, void *buf, guint buflen, gboolean copy_data, GError **error);
GIOStatus z_stream_write_packet(ZStream *s, ZPktBuf *packet, GError **error);

ZStream *z_stream_buf_new(ZStream *stream, gsize bufsize_threshold, guint32 flags);
void z_stream_buf_flush(ZStream *stream);

#ifdef __cplusplus
}
#endif

#endif
