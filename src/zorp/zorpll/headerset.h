/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/
#ifndef ZORP_HEADERSET_H
#define ZORP_HEADERSET_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ZHeader
{
  GString *key;
  GString *value;
} ZHeader;

typedef struct _ZHeaderSet
{
  gint headers_count;
  GHashTable *headers;
} ZHeaderSet;

GList *z_header_set_get_all_headers(ZHeaderSet *self);
ZHeader *z_header_set_iterate(ZHeaderSet *self, const gchar *key, gpointer *opaque);
gboolean z_header_set_add(ZHeaderSet *self, GString *key, GString *value, gboolean multiple);
void z_header_set_init(ZHeaderSet *self);
void z_header_set_destroy(ZHeaderSet *self);

static inline ZHeader *
z_header_set_find(ZHeaderSet *self, gchar *key)
{
  return z_header_set_iterate(self, key, NULL);
}

static inline gint
z_header_set_get_count(ZHeaderSet *self)
{
  return self->headers_count;
}

#ifdef __cplusplus
}
#endif

#endif
