/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_PROCESS_H_INCLUDED
#define ZORP_PROCESS_H_INCLUDED

#include <zorpll/zorplib.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef G_OS_WIN32

#include <sys/types.h>

typedef enum
{
  Z_PM_FOREGROUND,
  Z_PM_BACKGROUND,
  Z_PM_SAFE_BACKGROUND,
  Z_PM_SYSTEMD_NOTIFY,
} ZProcessMode;

void z_process_message(const gchar *fmt, ...);
gboolean z_resolve_user(const gchar *user, uid_t *gid);
gboolean z_resolve_group(const gchar *group, gid_t *gid);

void z_process_set_mode(ZProcessMode mode);
void z_process_set_name(const gchar *name);
void z_process_set_user(const gchar *user);
void z_process_set_group(const gchar *group);
void z_process_set_chroot(const gchar *chroot);
void z_process_set_pidfile(const gchar *pidfile);
void z_process_set_pidfile_dir(const gchar *pidfile_dir);
void z_process_set_working_dir(const gchar *cwd);
void z_process_set_caps(const gchar *caps);
void z_process_set_argv_space(gint argc, gchar **argv);
void z_process_set_use_fdlimit(gboolean use);
void z_process_set_check(gint check_period, gboolean (*check_fn)(void));
void z_process_set_check_enable(gboolean new_state);
gboolean z_process_get_check_enable(void);
ZProcessMode z_process_get_mode();

void z_process_start(void);
void z_process_startup_failed(guint ret_num, gboolean may_exit);
void z_process_startup_ok(void);
void z_process_finish(void);
void z_process_finish_prepare(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
