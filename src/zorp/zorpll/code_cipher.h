#ifndef ZORP_CODE_CIPHER_H_INCLUDED
#define ZORP_CODE_CIPHER_H_INCLUDED

#include <zorpll/code.h>
#include <openssl/evp.h>

#ifdef __cplusplus
extern "C" {
#endif

ZCode *z_code_cipher_new(EVP_CIPHER_CTX *cipher_ctx);

#ifdef __cplusplus
}
#endif

#endif
