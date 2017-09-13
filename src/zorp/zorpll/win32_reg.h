/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_WIN32_REGISTRY_H_INCLUDED
#define ZORP_WIN32_REGISTRY_H_INCLUDED

#ifdef G_OS_WIN32

#include <glib.h>
#include <windows.h>
#include <winreg.h>

#ifdef __cplusplus
extern "C" {
#endif

gboolean z_reg_key_write_dword(HKEY root, gchar *key, gchar *name, DWORD value);
gboolean z_reg_key_write_string(HKEY root, gchar *key, gchar *name, gchar *value);

gboolean z_reg_key_read_dword(HKEY root, gchar *key, gchar *name, DWORD *value);
gboolean z_reg_key_read_string(HKEY root, gchar *key, gchar *name, gchar **value);
gboolean z_reg_key_delete(HKEY root, gchar *key, gchar *name);
gboolean z_sid_to_text( PSID ps, char *buf, int bufSize );

#ifdef __cplusplus
}
#endif

#endif

#endif
