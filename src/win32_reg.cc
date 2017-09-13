/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/
#include <glib.h>

#include <glib.h>
#ifdef G_OS_WIN32

#include <zorpll/win32_reg.h>
#include <string.h>

#include <windows.h>
#include <winreg.h>

/**
 * This function stores data in the value field of an registry key.
 *
 * @param[in] root The key that is opened or created by this function is a subkey of the key that is identified by this parameter.
 * @param[in] key Pointer to a null-terminated string that specifies the name of a subkey that this function opens or creates.
 * This is a subkey of the key that is identified by <TT>root</TT>.
 * @param[in] name Pointer to a string containing the name of the value to set.
 * @param[in] value The data to be stored with the specified value name.
*/
gboolean
z_reg_key_write_dword(HKEY root, gchar *key, gchar *name, DWORD value)
{
  HKEY hKey;

  if(RegCreateKeyEx(root, (LPCSTR)key, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
      return FALSE;
    }

  if(RegSetValueEx(hKey, (LPCSTR)name, 0, REG_DWORD, (LPBYTE)&value, sizeof(DWORD)) == ERROR_SUCCESS)
    {
      RegFlushKey(hKey);
      RegCloseKey(hKey);
      return TRUE;
    }

  RegCloseKey(hKey);
  return FALSE;
}

/**
 * This function stores data in the value field of an registry key.
 *
 * @param[in] root The key that is opened or created by this function is a subkey of the key that is identified by this parameter.
 * @param[in] key Pointer to a null-terminated string that specifies the name of a subkey that this function opens or creates.
 * This is a subkey of the key that is identified by <TT>root</TT>.
 * @param[in] name Pointer to a string containing the name of the value to set.
 * @param[in] value Pointer to a buffer that contains the data to be stored with the specified value name.
*/
gboolean
z_reg_key_write_string(HKEY root, gchar *key, gchar *name, gchar *value)
{
  HKEY hKey;

  if(RegCreateKeyEx(root, (LPCSTR)key, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    return FALSE;

  if(RegSetValueEx(hKey, (LPCSTR)name, 0, REG_SZ, (LPBYTE)value, strlen(value)+1) == ERROR_SUCCESS)
    {
      RegFlushKey(hKey);
      RegCloseKey(hKey);
      return TRUE;
    }

  RegCloseKey(hKey);
  return FALSE;
}

/**
 * This function stores data in the value field of an registry key.
 *
 * @param[in] root The key that is removed by this function is a subkey of the key that is identified by this parameter.
 * @param[in] key Pointer to a null-terminated string that specifies the name of a subkey that this function removes.
 * This is a subkey of the key that is identified by <TT>root</TT>.
 * @param[in] name Pointer to a null-terminated string that names the value to remove.
*/
gboolean
z_reg_key_delete(HKEY root, gchar *key, gchar *name)
{
  HKEY hKey;

  if(RegOpenKeyEx(root,(LPCSTR)key, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
    return FALSE;

  if(RegDeleteValue(hKey, (LPCSTR)name) != ERROR_SUCCESS)
    {
      RegFlushKey(hKey);
      RegCloseKey(hKey);
      return TRUE;
    }

  RegCloseKey(hKey);
  return FALSE;
}

/**
 * This function retrieves the data for a specified value associated with a specified registry key.
 *
 * @param[in] root The key that is opened or created by this function is a subkey of the key that is identified by this parameter.
 * @param[in] key Pointer to a null-terminated string that specifies the name of a subkey that this function opens or creates.
 * This is a subkey of the key that is identified by <TT>root</TT>.
 * @param[in] name Pointer to a string containing the name of the value to set.
 * @param[in, out] value Pointer to a buffer that receives value data.
*/
gboolean
z_reg_key_read_dword(HKEY root, gchar *key, gchar *name, DWORD *value)
{
  HKEY hKey;
  DWORD t = sizeof(DWORD);
  DWORD type = REG_DWORD;

  if(RegOpenKeyEx(root,(LPCSTR)key, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
    return FALSE;

  if(RegQueryValueEx(hKey, (LPCSTR)name, 0, &type, (LPBYTE)value, &t) == ERROR_SUCCESS)
    {
      RegCloseKey(hKey);
      return TRUE;
    }
  RegCloseKey(hKey);
  return FALSE;
}

/**
 * This function retrieves the data for a specified value associated with a specified registry key.
 *
 * @param[in] root The key that is opened or created by this function is a subkey of the key that is identified by this parameter.
 * @param[in] key Pointer to a null-terminated string that specifies the name of a subkey that this function opens or creates.
 * This is a subkey of the key that is identified by <TT>root</TT>.
 * @param[in] name Pointer to a string containing the name of the value to set.
 * @param[in, out] value Pointer to a buffer that receives value data.
*/
gboolean
z_reg_key_read_string(HKEY root, gchar *key, gchar *name, gchar **value)
{
  HKEY hKey;
  gchar temp[2000];
  DWORD t = 2000;
  DWORD type = REG_SZ;

  if(RegOpenKeyEx(root,(LPCSTR)key, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
    return FALSE;

  if(RegQueryValueEx(hKey, (LPCSTR)name, 0, &type, (LPBYTE)&temp, &t) == ERROR_SUCCESS)
    {
      *value = g_strdup(temp);
      RegCloseKey(hKey);
      return TRUE;
    }

  RegCloseKey(hKey);
  return FALSE;
}

gboolean
z_sid_to_text( PSID ps, char *buf, int bufSize )
{
  PSID_IDENTIFIER_AUTHORITY psia;
  DWORD dwSubAuthorities;
  DWORD dwSidRev = SID_REVISION;
  DWORD i;
  int n, size;
  char *p;

  if ( ! IsValidSid( ps ) )
    return FALSE;

  psia = GetSidIdentifierAuthority( ps );

  dwSubAuthorities = *GetSidSubAuthorityCount( ps );

  size = 15 + 12 + ( 12 * dwSubAuthorities ) + 1;

  if ( bufSize < size )
    {
      SetLastError( ERROR_INSUFFICIENT_BUFFER );
      return FALSE;
    }

  size = wsprintf( buf, "S-%lu-", dwSidRev );
  p = buf + size;

  if ( psia->Value[0] != 0 || psia->Value[1] != 0 )
    {
      n = wsprintf( p, "0x%02hx%02hx%02hx%02hx%02hx%02hx",
                    (USHORT) psia->Value[0], (USHORT) psia->Value[1],
                    (USHORT) psia->Value[2], (USHORT) psia->Value[3],
                    (USHORT) psia->Value[4], (USHORT) psia->Value[5] );
      size += n;
      p += n;
    }
  else
    {
      n = wsprintf( p, "%lu", ( (ULONG) psia->Value[5] ) +
                    ( (ULONG) psia->Value[4] << 8 ) + ( (ULONG) psia->Value[3] << 16 ) +
                    ( (ULONG) psia->Value[2] << 24 ) );
      size += n;
      p += n;
    }

  for ( i = 0; i < dwSubAuthorities; ++ i )
    {
      n = wsprintf( p, "-%lu", *GetSidSubAuthority( ps, i ) );
      size += n;
      p += n;
    }

  return TRUE;
}

#endif
