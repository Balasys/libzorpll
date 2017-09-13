/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/registry.h>
#include <zorpll/log.h>

#include <glib.h>
#include <string.h>

static GHashTable *registry[MAX_REGISTRY_TYPE];

#define MAX_REGISTRY_NAME 32

/**
 * ZRegistryEntry is an entry in the registry hash.
 *
 * An entry in the registry. The registry is a hash table indexed by a
 * string and storing an opaque pointer and an integer type
 * value. 
 */
typedef struct _ZRegistryEntry
{
  gint type;
  gchar name[MAX_REGISTRY_NAME];
  gpointer value;
} ZRegistryEntry;

/**
 * Initialize the registry.
 **/
void
z_registry_init(void)
{
  int i;
  for (i = 0; i < MAX_REGISTRY_TYPE; i++)
    registry[i] = g_hash_table_new(g_str_hash, g_str_equal);
}


/**
 * Deinitialize the registry subsystem.
 **/
void
z_registry_destroy(void)
{
  int i;
  for (i = 0; i<MAX_REGISTRY_TYPE; i++)
    g_hash_table_destroy(registry[i]);
}

static gboolean
z_registry_is_type_valid(gint type)
{
  return (type >= 0 && type < MAX_REGISTRY_TYPE);
}

/**
 * Add an entry to the registry with the given name and type.
 *
 * @param[in] name the key of this entry
 * @param[in] type the type of this entry
 * @param[in] value the pointer associated with this entry
 **/
void
z_registry_add(const gchar *name, gint type, gpointer value)
{
  ZRegistryEntry *ze = g_new0(ZRegistryEntry, 1);
  
  if (!z_registry_is_type_valid(type))
    {
      /*LOG
        This message indicates that an internal error occurred,
        a buggy/incompatible loadable module wanted to register
        an unsupported module type. Please report this event to the 
	 Balabit QA Team (devel@balabit.com).
       */
      z_log(NULL, CORE_ERROR, 0, "Internal error, bad registry type; name='%s', type='%d'", name, type);
      return;
    }
  g_strlcpy(ze->name, name, sizeof(ze->name));
  ze->value = value;
  ze->type = type;
  g_hash_table_insert(registry[type], ze->name, ze);
}

/**
 * Fetch an item from the registry with the given name returning its
 * type and pointer.
 *
 * @param[in] name name of the entry to fetch
 * @param[in] type type of the entry
 *
 * @returns NULL if the entry was not found, the associated pointer otherwise
 **/
static ZRegistryEntry *
z_registry_get_one(const gchar *name, gint type)
{
  ZRegistryEntry *ze = NULL;
  
  z_enter();
  ze = static_cast<ZRegistryEntry *>(g_hash_table_lookup(registry[type], name));
  z_return(ze);
}

/**
 * This function returns an entry from the registry autoprobing for
 * different types.
 *
 * @param[in]      name name of the entry to fetch
 * @param[in, out] type contains the preferred entry type on input, contains the real type on output
 *
 * If type is NULL or the value pointed by type is 0,
 * then each possible entry type is checked, otherwise only the value
 * specified will be used.
 *
 * @returns the value stored in the registry or NULL if nothing is found
 **/
gpointer
z_registry_get(const gchar *name, gint *type)
{
  int i;
  ZRegistryEntry *ze = NULL;

  z_enter();
  if (type && !z_registry_is_type_valid(*type))
    z_return(NULL);

  if (type == NULL || *type == 0)
    {
      for (i = 0; i < MAX_REGISTRY_TYPE && ze == NULL; i++)
        {
          ze = z_registry_get_one(name, i);
        }
    }
  else
    {
      ze = z_registry_get_one(name, *type);
    }
    
  if (ze)
    {
      if (type) 
        *type = ze->type;
      z_return(ze->value);
    }
  z_return(NULL);
}

/**
 * This function checks whether the given name is found in the registry at
 * all, and returns the accompanying type value if found.
 *
 * @param[in] name name to search for
 *
 * @return the type value
 **/
guint
z_registry_has_key(const gchar *name)
{
  int i;
  ZRegistryEntry *ze = NULL;
  
  for (i = 0; (i < MAX_REGISTRY_TYPE) && (ze == NULL); i++)
    {
      ze = z_registry_get_one(name, i);
    }
  if (ze)
    {
      return i;
    }
  else
    {
      return 0;
    }
}

typedef struct _ZRegistryForeachCallbackData
{
  ZRFunc user_func;
  gpointer user_data;
} ZRegistryForeachCallbackData;

static void
z_registry_foreach_invoke_callback(gpointer key G_GNUC_UNUSED, gpointer value, gpointer user_data)
{
  ZRegistryForeachCallbackData *callback_data = (ZRegistryForeachCallbackData *)user_data;
  ZRegistryEntry *ze = (ZRegistryEntry *) value;

  if (ze)
    (*(callback_data->user_func))(ze->name, ze->type, ze->value, callback_data->user_data);
}

/**
 * This function iterates over the set of registry entries having the
 * type type.
 *
 * @param[in] type type of entries to iterate over
 * @param[in] func a ZRFunc function to call for elements
 **/
void
z_registry_foreach(gint type, ZRFunc func, gpointer user_data)
{
  ZRegistryForeachCallbackData helper_data;

  g_assert(z_registry_is_type_valid(type));

  helper_data.user_func = func;
  helper_data.user_data = user_data;

  g_hash_table_foreach(registry[type], z_registry_foreach_invoke_callback, &helper_data);
}
