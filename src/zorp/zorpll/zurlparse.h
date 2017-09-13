#ifndef ZORP_ZURLPARSE_H_INCLUDED
#define ZORP_ZURLPARSE_H_INCLUDED

#include <zorpll/zorplib.h>

#define ZURL_ERROR z_url_error_quark()

#ifdef __cplusplus
extern "C" {
#endif


GQuark z_url_error_quark(void);

enum ZURLError
{
  ZURL_ERROR_FAILED,
};

typedef struct _ZURL
{
  /* all gstrings below might contain NUL characters as they store the URL-decoded form */
  GString *scheme;
  GString *user;
  GString *passwd;
  GString *host;
  guint port;
  GString *file;
} ZURL;

gboolean z_url_parse(ZURL *self, const gchar *url_str, GError **error);
void z_url_init(ZURL *self);
void z_url_free(ZURL *self);

#ifdef __cplusplus
}
#endif


#endif
