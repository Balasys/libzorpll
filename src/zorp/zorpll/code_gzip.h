#ifndef ZORP_PROXY_CODE_GZIP_H
#define ZORP_PROXY_CODE_GZIP_H

#include <zorpll/code.h>

#ifdef __cplusplus
extern "C" {
#endif

ZCode *z_code_gzip_encode_new(gint bufsize, gint compress_level);
ZCode *z_code_gzip_decode_new(gint bufsize);

#ifdef __cplusplus
}
#endif

#endif
