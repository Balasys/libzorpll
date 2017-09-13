/***************************************************************************
 *
 * COPYRIGHTHERE
 *
 ***************************************************************************/
#ifndef ZORP_ZCP_H
#define ZORP_ZCP_H

#include <zorpll/headerset.h>
#include <zorpll/stream.h>

#define ZCP_LINE_LENGTH 16384

#ifdef __cplusplus
extern "C" {
#endif

typedef ZHeader ZCPHeader;

typedef struct _ZCPCommand
{
  GString *command;
  ZHeaderSet headers;
} ZCPCommand;

typedef struct _ZCPContext ZCPContext;


static inline GList *
z_cp_command_get_all_headers(ZCPCommand *self)
{
  return z_header_set_get_all_headers(&self->headers);
}

static inline ZCPHeader *
z_cp_command_iterate_headers(ZCPCommand *self,
                             const gchar *key,
                             gpointer *opaque)
{
  return z_header_set_iterate(&self->headers, key, opaque);
}

static inline ZCPHeader *
z_cp_command_find_header(ZCPCommand *self, const gchar *key)
{
  return z_cp_command_iterate_headers(self, key, NULL);
}

static inline gboolean
z_cp_command_add_header(ZCPCommand *self,
                        GString *key,
                        GString *value,
                        gboolean multiple)
{
  return z_header_set_add(&self->headers, key, value, multiple);
}

ZCPCommand *z_cp_command_new(const gchar *cmd);
ZCPCommand *z_cp_command_new_accept(const gchar *welcome, GSList *groups);
ZCPCommand *z_cp_command_new_reject(const gchar *reason);
void z_cp_command_free(ZCPCommand *self);


GIOStatus z_cp_context_read(ZCPContext *self, guint *session_id, ZCPCommand **cmd);
GIOStatus z_cp_context_write(ZCPContext *self, guint session_id, ZCPCommand *cmd);

ZCPContext *z_cp_context_new(ZStream *stream);
void z_cp_context_destroy(ZCPContext *self, gboolean close_stream);

#ifdef __cplusplus
}
#endif


#endif
