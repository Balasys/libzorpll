/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef _ZORP_ERROR_H_INCLUDED
#define _ZORP_ERROR_H_INCLUDED

#include <zorpll/zorplib.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#endif

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

gboolean z_errno_is(int e);
int z_errno_get(void);
void z_errno_set(int e);

#ifdef __cplusplus
}
#endif

#endif
