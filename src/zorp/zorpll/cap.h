/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_CAP_H_INCLUDED
#define ZORP_CAP_H_INCLUDED


#include <zorpll/zorplib.h>

#ifdef __cplusplus
extern "C" {
#endif

#if ZORPLIB_ENABLE_CAPS

#include <sys/capability.h>

extern const gchar *zorp_caps;

gboolean cap_modify(int capability, int onoff);
cap_t cap_save(void);
gboolean cap_restore(cap_t r);

#define cap_enable(cap) cap_modify(cap, TRUE)
#define cap_disable(cap) cap_modify(cap, FALSE)

#else

typedef int cap_t;

#define CAP_NET_ADMIN 0
#define CAP_NET_BIND_SERVICE 0

#define cap_save() 0
#define cap_restore(r)
#define cap_enable(cap)
#define cap_disable(cap)

#endif

#ifdef __cplusplus
}
#endif

#endif

