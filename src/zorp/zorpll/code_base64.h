#ifndef ZORP_PROXY_CODE_BASE64_H
#define ZORP_PROXY_CODE_BASE64_H

#include <zorpll/code.h>

#ifdef __cplusplus
extern "C" {
#endif

ZCode *z_code_base64_encode_new(gint bufsize, gint linelen);
ZCode *z_code_base64_decode_new(gint bufsize, gboolean error_tolerant);

#ifdef __cplusplus
}
#endif

#endif
