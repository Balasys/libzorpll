/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_SOURCE_H_INCLUDED
#define ZORP_SOURCE_H_INCLUDED

#include <zorpll/stream.h>
#include <zorpll/zorplib.h>
#include <zorpll/io.h>

#ifdef __cplusplus
extern "C" {
#endif

GSource *z_threshold_source_new(guint idle_threshold, guint busy_threshold);
void z_threshold_source_set_threshold(GSource *source, guint idle_threshold, guint busy_threshold);

void z_timeout_source_set_timeout(GSource *s, gulong new_timeout);
void z_timeout_source_set_time(GSource *source, GTimeVal *nexttime);
GSource *z_timeout_source_new(gulong initial_timeout);
void z_timeout_source_disable(GSource *source);

#ifdef __cplusplus
}
#endif

#endif
