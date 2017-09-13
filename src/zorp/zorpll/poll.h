/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_POLL_H_INCLUDED
#define ZORP_POLL_H_INCLUDED

#include <zorpll/zorplib.h>
#include <zorpll/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ZPoll interface encapsulates a poll() loop.
 **/
typedef struct _ZPoll ZPoll;

ZPoll *z_poll_new(void);
void z_poll_ref(ZPoll *);
void z_poll_unref(ZPoll *);

void z_poll_wakeup(ZPoll *s);

void z_poll_add_stream(ZPoll *s, struct _ZStream *channel);

void z_poll_remove_stream(ZPoll *s, ZStream *stream);

#define z_poll_iter(s) z_poll_iter_timeout(s, -1)

guint z_poll_iter_timeout(ZPoll *s, gint timeout);

gboolean z_poll_is_running(ZPoll *s);

void z_poll_quit(ZPoll *s);

GMainContext *z_poll_get_context(ZPoll *s);

#ifdef __cplusplus
}
#endif

#endif
