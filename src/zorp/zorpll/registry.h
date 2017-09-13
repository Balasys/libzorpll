/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_REGISTRY_H_INCLUDED
#define ZORP_REGISTRY_H_INCLUDED

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PROXY_NAME 32

#define ZR_NONE      0
#define ZR_PROXY     1
#define ZR_PYPROXY   2
/* deprecated
#define ZR_DPROXY    3
*/
#define ZR_CONNTRACK 4
#define ZR_OTHER     5
#define ZR_MODULE    6
#define MAX_REGISTRY_TYPE 16

/**
 * Specifies the type of the function passed to z_registry_foreach().
 * It is called for each entry, with the name, type and the value stored,
 * and the user_data passed to z_registry_foreach.
 **/
typedef void (*ZRFunc)(const gchar *name, gint type, gpointer value, gpointer user_data);

void z_registry_init(void);
void z_registry_destroy(void);
void z_registry_add(const gchar *name, gint type, gpointer value);
gpointer z_registry_get(const gchar *name, gint *type);
guint z_registry_has_key(const gchar *name);
void z_registry_foreach(gint type, ZRFunc func, gpointer user_data);

#ifdef __cplusplus
}
#endif

#endif
