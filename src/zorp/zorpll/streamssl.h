/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_STREAMSSL_H_INCLUDED
#define ZORP_STREAMSSL_H_INCLUDED

#include <zorpll/ssl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZST_CTRL_SSL_SET_SESSION     (0x01) | ZST_CTRL_SSL_OFS
#define ZST_CTRL_SSL_ADD_HANDSHAKE   (0x02) | ZST_CTRL_SSL_OFS

LIBZORPLL_EXTERN ZClass ZStreamSsl__class;

ZStream * z_stream_ssl_new(ZStream *stream, ZSSLSession *ssl);

static inline void
z_stream_ssl_set_session(ZStream *self, ZSSLSession *ssl)
{
  z_stream_ctrl(self, ZST_CTRL_SSL_SET_SESSION, ssl, sizeof(&ssl));
}

typedef struct
{
  gpointer handshake;
  GDestroyNotify destroy_function;
} ZStreamSslHandshakeData;

static inline void
z_stream_ssl_add_handshake(ZStream *self, gpointer handshake, GDestroyNotify destroy)
{
  ZStreamSslHandshakeData data;

  data.handshake = handshake;
  data.destroy_function = destroy;

  z_stream_ctrl(self, ZST_CTRL_SSL_ADD_HANDSHAKE, &data, sizeof(data));
}

#ifdef __cplusplus
}
#endif

#endif /* ZORP_GIOSSL_H_INCLUDED */
