/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/headerset.h>

#include <zorpll/log.h>


static void
z_header_set_destroy_chain(GList *list)
{
  ZHeader *header;

  z_enter();
  while (list)
    {
      header = (ZHeader *)list->data;
      g_string_free(header->key, TRUE);
      g_string_free(header->value, TRUE);
      g_free(header);
      list = g_list_delete_link(list, list);
    }
  z_return();
}

static void
z_header_set_destroy_foreach(gpointer key G_GNUC_UNUSED,
                                         gpointer value,
                                         gpointer user_data G_GNUC_UNUSED)
{
  z_enter();
  z_header_set_destroy_chain((GList *)value);
  z_return();
}

static gint
z_header_compare(ZHeader *h1, ZHeader *h2)
{
  return strcmp(h2->key->str, h1->key->str);
}

static void
z_header_set_append_foreach(gpointer key G_GNUC_UNUSED,
                                       gpointer value,
                                       gpointer user_data)
{
  GList **ret = (GList **) user_data;
  GList *list;
  ZHeader *header;

  list = (GList *) value;

  while (list)
    {
      header = static_cast<ZHeader *>(list->data);
      *ret = g_list_insert_sorted(*ret, header, (GCompareFunc) z_header_compare);
      list = g_list_next(list);
    }
}

GList *
z_header_set_get_all_headers(ZHeaderSet *self)
{
  GList *ret = NULL;

  z_enter();
  g_hash_table_foreach(self->headers,
                       z_header_set_append_foreach,
                       &ret);
  ret = g_list_reverse(ret);
  z_return(ret);
}

ZHeader *
z_header_set_iterate(ZHeaderSet *self, const gchar *key, gpointer *opaque)
{
  ZHeader *res;
  GList *value = opaque ? (GList *) *opaque : NULL;

  z_enter();
  if (value == NULL)
    value = (GList *) g_hash_table_lookup(self->headers, key);
  else
    value = g_list_next(value);

  if (value == NULL)
    z_return(NULL);

  res = static_cast<ZHeader *>(value->data);
  if (opaque)
    *opaque = value;
  z_return(res);
}


gboolean
z_header_set_add(ZHeaderSet *self,
                 GString *key,
                 GString *value,
                 gboolean multiple)
{
  ZHeader *header;
  GList *hlist;

  z_enter();
  header = g_new0(ZHeader, 1);
  header->key = key;
  header->value = value;
  hlist = static_cast<GList *>(g_hash_table_lookup(self->headers, header->key->str));
  if (!hlist || (header->key->str[0] == 'X') || multiple)
    {
      self->headers_count++;
      hlist = g_list_append(hlist, header);
      g_hash_table_insert(self->headers, header->key->str, hlist);
      z_return(TRUE);
    }
  z_return(FALSE);
}

void
z_header_set_init(ZHeaderSet *self)
{
  self->headers = g_hash_table_new(g_str_hash, g_str_equal);
}

void
z_header_set_destroy(ZHeaderSet *self)
{
  g_hash_table_foreach(self->headers,
                       z_header_set_destroy_foreach,
                       NULL);
  g_hash_table_destroy(self->headers);
  self->headers = NULL;
}
