/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <zorpll/process.h>
 
#ifndef G_OS_WIN32

#include <zorpll/cap.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <pwd.h>
#include <grp.h>

#include <zorpll/misc.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#if HAVE_SYSTEMD == 1
#include <systemd/sd-daemon.h>
#include <zorpll/thread.h>
#include <zorpll/log.h>
#endif

/**
 * @file
 *
 * @note
 * 
 * - pidfile is created and removed by the daemon (e.g. the child) itself,
 *   the parent does not touch that
 *
 * - we communicate with the user using stderr (using fprintf) as long as it
 *   is available and using syslog() afterwards
 *
 * - there are 3 processes involved in safe_background mode (e.g. auto-restart)
 *   - startup process which was started by the user (zorpctl)
 *   - supervisor process which automatically restarts the daemon when it exits abnormally
 *   - daemon processes which perform the actual task at hand
 *   
 *   The startup process delivers the result of the first startup to its
 *   caller, if we can deliver a failure in this case then restarts will not
 *   be performed (e.g. if the first startup fails, the daemon will not be
 *   restarted even if auto-restart was enabled). After the first successful
 *   start, the startup process exits (delivering that startup was
 *   successful) and the supervisor process wait()s for the daemon processes
 *   to exit. If they exit prematurely (e.g. they crash) they will be
 *   restarted, if the startup is not successful in this case the restart
 *   will be attempted again just as if they crashed.
 *
 *   The processes communicate with two pairs of pipes, startup_result_pipe
 *   is used to indicate success/failure to the startup process,
 *   init_result_pipe (as in "initialization") is used to deliver success
 *   reports from the daemon to the supervisor.
 **/
 
 
typedef enum
{
  Z_PK_STARTUP,
  Z_PK_SUPERVISOR,
  Z_PK_DAEMON,
} ZProcessKind;

#define Z_PROCESS_FD_LIMIT_RESERVE 64
#define Z_PROCESS_FAILURE_NOTIFICATION ZORPLIB_LIBEXECDIR "/failure_notify" ZORPLIB_COMPAT_BRANCH

/** pipe used to deliver the initialization result to the calling process */
static gint startup_result_pipe[2] = { -1, -1 };
/** pipe used to deliver initialization result to the supervisor */
static gint init_result_pipe[2] = { -1, -1 };
static ZProcessKind process_kind = Z_PK_STARTUP;
static gboolean stderr_present = TRUE;

extern gint max_threads;

/**
 * Resolve a UNIX user name and store the associated uid in uid.
 *
 * @param[in]  user username or numeric uid if there's no such username
 * @param[out] uid the uid of user
 *
 * @returns TRUE to indicate success
 **/
gboolean
z_resolve_user(const gchar *user, uid_t *uid)
{
  struct passwd pw;
  struct passwd *pw_p;
  gchar buf[1024];

  *uid = 0;
  if (getpwnam_r(user, &pw, buf, sizeof(buf), &pw_p) == 0 && pw_p)
    {
      *uid = pw.pw_uid;
    }
  else
    {
      gchar *err;
      gulong tmp_uid;
      
      tmp_uid = strtol(user, &err, 0);
      if (*err != '\0')
        return FALSE;
      *uid = (uid_t)tmp_uid;
    }
  return TRUE;
}

/**
 * Resolve a UNIX group and store the associated gid in gid.
 *
 * @param[in] group groupname or numeric gid if there's no such groupname
 * @param[out] gid gid of group
 *
 * @returns TRUE to indicate success
 **/
gboolean
z_resolve_group(const gchar *group, gid_t *gid)
{
  struct group gr;
  struct group *gr_p;
  gchar buf[1024];

  *gid = 0;
  if (getgrnam_r(group, &gr, buf, sizeof(buf), &gr_p) == 0 && gr_p)
    {
      *gid = gr.gr_gid;
    }
  else
    {
      gchar *err;
      gulong tmp_gid;
      
      tmp_gid = strtol(group, &err, 0);
      if (*err != '\0')
        return FALSE;
      *gid = (gid_t)tmp_gid;
    }
  return TRUE;
}

/** global variables */
static struct
{
  ZProcessMode mode;
  gint  argc;
  gchar **argv;
  gchar *argv_start;
  size_t argv_env_len;
  gboolean use_fdlimit_settings;
  gint fd_limit_threshold;
  gint fd_limit_min;
  gint check_period;
  gboolean (*check_fn)(void);
  gint restart_max;
  gint restart_interval;
  gint notify_interval;
  gboolean pid_removed;

  const gchar *name;
  const gchar *user;
  const gchar *group;
  const gchar *chroot_dir;
  const gchar *pidfile;
  const gchar *pidfile_dir;
  const gchar *cwd;
  const gchar *caps;
  gboolean nocaps;
  gchar *argv_orig;
  gboolean core;
} process_opts =
{
  Z_PM_SAFE_BACKGROUND,
  0,
  NULL,
  NULL,
  0,
  FALSE,
  -1,
  256000,
  -1,
  NULL,
  5,
  60,
  600,
  FALSE
};

/**
 * This function should be called by the daemon to set the processing mode
 * as specified by mode.
 *
 * @param[in] mode an element from ZProcessMode
 **/
void 
z_process_set_mode(ZProcessMode mode)
{
  process_opts.mode = mode;
}

/**
 * Called by the daemon to set the program name.
 *
 * @param[in] name the name of the process to be reported as program name
 *
 * This function should be called by the daemon to set the program name
 * which is present in various error message and might influence the PID
 * file if not overridden by z_process_set_pidfile().
 **/
void 
z_process_set_name(const gchar *name)
{
  process_opts.name = name;
}

/**
 * This function should be called by the daemon to set the user name.
 *
 * @param[in] user the name of the user the process should switch to during startup
 **/
void 
z_process_set_user(const gchar *user)
{
  if (!process_opts.user)
    process_opts.user = user;
}

/**
 * This function should be called by the daemon to set the group name.
 *
 * @param[in] group the name of the group the process should switch to during startup
 **/
void 
z_process_set_group(const gchar *group)
{
  if (!process_opts.group)
    process_opts.group = group;
}

/**
 * This function should be called by the daemon to set the chroot directory
 *
 * @param[in] chroot_dir the name of the chroot directory the process should switch to during startup
 **/
void 
z_process_set_chroot(const gchar *chroot_dir)
{
  if (!process_opts.chroot_dir)
    process_opts.chroot_dir = chroot_dir;
}

/**
 * Called by the daemon to set the PID file name and store the PID.
 *
 * @param[in] pidfile the name of the complete pid file with full path
 *
 * This function should be called by the daemon to set the PID file name to
 * store the pid of the process. This value will be used as the pidfile
 * directly, neither name nor pidfile_dir influences the pidfile location if
 * this is set.
 **/
void 
z_process_set_pidfile(const gchar *pidfile)
{
  if (!process_opts.pidfile)
    process_opts.pidfile = pidfile;
}

/**
 * This function should be called by the daemon to set the PID file
 * directory.
 *
 * @param[in] pidfile_dir name of the pidfile directory
 *
 * This value is not used if set_pidfile() was called.
 **/
void 
z_process_set_pidfile_dir(const gchar *pidfile_dir)
{
  if (!process_opts.pidfile_dir)
    process_opts.pidfile_dir = pidfile_dir;
}

/**
 * This function should be called by the daemon to set the working
 * directory.
 *
 * @param[in] cwd name of the working directory
 *
 * The process will change its current directory to this value or
 * to pidfile_dir if it is unset.
 **/
void 
z_process_set_working_dir(const gchar *cwd)
{
  if (!process_opts.cwd)
    process_opts.cwd = cwd;
}


/**
 * This function should be called by the daemon to set the initial
 * capability set.
 *
 * @param[in] caps capability specification in text form
 *
 * The process will change its capabilities to this value
 * during startup, provided it has enough permissions to do so.
 **/
void 
z_process_set_caps(const gchar *caps)
{
  if (!process_opts.caps)
    process_opts.caps = caps;
}

/**
 * This function should be called by the daemon if it wants to enable
 * process title manipulation in the supervisor process.
 *
 * @param[in] argc Original argc, as received by the main function in its first parameter
 * @param[in] argv Original argv, as received by the main function in its second parameter
 **/
void
z_process_set_argv_space(gint argc, gchar **argv)
{
  extern char **environ;
  gchar *lastargv = NULL;
  gchar **envp    = environ;
  gint i;

  if (process_opts.argv)
    return;
  process_opts.argv = argv;
  process_opts.argc = argc;
    
  for (i = 0; envp[i] != NULL; i++)
    ;
  
  environ = g_new(char *, i + 1);

  /*
   * Find the last argv string or environment variable within
   * our process memory area.
   */
  for (i = 0; i < process_opts.argc; i++)
    {
      if (lastargv == NULL || lastargv + 1 == process_opts.argv[i])
        lastargv = process_opts.argv[i] + strlen(process_opts.argv[i]);
    }
  for (i = 0; envp[i] != NULL; i++)
    {
      if (lastargv + 1 == envp[i])
        lastargv = envp[i] + strlen(envp[i]);
    }

  process_opts.argv_start = process_opts.argv[0];
  process_opts.argv_env_len = lastargv - process_opts.argv[0] - 1;

  process_opts.argv_orig = static_cast<gchar *>(malloc(sizeof(gchar) * process_opts.argv_env_len));
  memcpy(process_opts.argv_orig, process_opts.argv_start, process_opts.argv_env_len);

  /*
   * Copy environment
   * XXX - will truncate env on strdup fail
   */
  for (i = 0; envp[i] != NULL; i++)
    environ[i] = g_strdup(envp[i]);
  environ[i] = NULL;
}

/**
 * Enable (true) or disable (false) the usage of the fdlimit settings.
 *
 * @param[in] use value
 *
 * By default it's disabled.
 **/
void
z_process_set_use_fdlimit(gboolean use)
{
  process_opts.use_fdlimit_settings = use;
}

/**
 * Installs a checker function that is called at the specified rate.
 *
 * @param[in] check_period check period in seconds
 * @param[in] check_fn checker function
 *
 * The checked process is allowed to run as long as this function
 * returns TRUE.
 **/
void
z_process_set_check(gint check_period, gboolean (*check_fn)(void))
{
  process_opts.check_period = check_period;
  process_opts.check_fn = check_fn;
}

/**
 * Turns deadlock checker function on or off, as specified.
 *
 * @param[in] new_state TRUE to enable, FALSE to disable
 *
 * Won't work if timeout was set to -1 originally (or if it gets set to -1 elsewhere at all).
 **/
void
z_process_set_check_enable(gboolean new_state)
{
  static gint old_value = -1;
  gint temp;

  if ((process_opts.check_period >= 0) && (new_state))        /* already on */
    return;
  if ((process_opts.check_period < 0) && (!new_state))        /* already off */
    return;

  /* requested state is not the current state -> toggle state */
  temp = old_value;
  old_value = process_opts.check_period;
  process_opts.check_period = temp;
}

/**
 * Returns whether deadlock checking is enabled.
 *
 * @returns TRUE if enabled
 **/
gboolean
z_process_get_check_enable(void)
{
  if (process_opts.check_period >= 0)
    return TRUE;
  return FALSE;
}

/**
 * Returns with the current processing mode.
 *
 * @returns current processing mode
 **/
ZProcessMode
z_process_get_mode()
{
  return process_opts.mode;
}

/**
 * Send a message to the client using stderr as long as it's available and using
 * syslog() if it isn't.
 *
 * @param[in] fmt format string
 * @param[in] ... arguments to fmt
 * 
 * This function sends a message to the client preferring to use the stderr
 * channel as long as it is available and switching to using syslog() if it
 * isn't. Generally the stderr channel will be available in the startup
 * process and in the beginning of the first startup in the
 * supervisor/daemon processes. Later on the stderr fd will be closed and we
 * have to fall back to using the system log.
 **/
void
z_process_message(const gchar *fmt, ...)
{
  gchar buf[2048];
  va_list ap;
  
  va_start(ap, fmt);
  g_vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (stderr_present)
    fprintf(stderr, "%s: %s\n", process_opts.name, buf);
  else
    {
      gchar name[32];
      
      g_snprintf(name, sizeof(name), "%s/%s", process_kind == Z_PK_SUPERVISOR ? "supervise" : "daemon", process_opts.name);
      openlog(name, LOG_PID, LOG_DAEMON);
      syslog(LOG_CRIT, "%s\n", buf);
      closelog();
    }
}

/**
 * This function is called from z_process_start() to detach from the
 * controlling tty.
 **/
static void
z_process_detach_tty(void)
{
  if (process_opts.mode != Z_PM_FOREGROUND && process_opts.mode != Z_PM_SYSTEMD_NOTIFY)
    {
      /* detach ourselves from the tty when not staying in the foreground */
      if (isatty(STDIN_FILENO))
        {
          ioctl(STDIN_FILENO, TIOCNOTTY, 0);
          setsid();
        }
    }
}

/**
 * Set fd limit.
 **/
static void
z_process_change_limits(void)
{
  struct rlimit limit;

  if (process_opts.fd_limit_threshold != -1)
    z_process_message("Setting fd-limit-threshold is deprecated, use fd-limit-min directly;");

  limit.rlim_cur = limit.rlim_max = process_opts.fd_limit_min;
  
  if (setrlimit(RLIMIT_NOFILE, &limit) < 0)
    z_process_message("Error setting file number limit; limit='%d'; error='%s'", process_opts.fd_limit_min, g_strerror(errno));
}

/**
 * Use /dev/null as input/output/error. This function is idempotent, can be
 * called any number of times without harm.
 **/
static void
z_process_detach_stdio(void)
{
  gint devnull_fd;

  if (process_opts.mode != Z_PM_FOREGROUND && process_opts.mode != Z_PM_SYSTEMD_NOTIFY && stderr_present)
    {
      devnull_fd = open("/dev/null", O_RDONLY);
      if (devnull_fd >= 0)
        {
          dup2(devnull_fd, STDIN_FILENO);
          close(devnull_fd);
        }
      devnull_fd = open("/dev/null", O_WRONLY);
      if (devnull_fd >= 0)
        {
          dup2(devnull_fd, STDOUT_FILENO);
          dup2(devnull_fd, STDERR_FILENO);
          close(devnull_fd);
        }
      stderr_present = FALSE;
    }
}

/**
 * Enable core file dumping by setting PR_DUMPABLE and changing the core
 * file limit to infinity.
 **/
static void
z_process_enable_core(void)
{
  if (!process_opts.core)
    return;

  int rc = -1;
#if HAVE_PRCTL
  /**
   * return code <  0 means error
   * return code == 1 means dumpable with user to the current working directory
   * return code == 2 means dumpable, but dump
   *                  can only dumped with fully qualified path
   *                  can be disabled by suid_dumpable in proc
   * return code != 1 means not proper working in our point of view
  **/
  if ((rc = prctl(PR_GET_DUMPABLE, 0, 0, 0, 0)) != 1)
      rc = prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);
#endif
   if (rc < 0)
     z_process_message("Cannot set process to be dumpable; error='%s'", g_strerror(errno));

  struct rlimit limit;
  limit.rlim_cur = limit.rlim_max = RLIM_INFINITY;
  if (setrlimit(RLIMIT_CORE, &limit) < 0)
    z_process_message("Error setting core limit to infinity; error='%s'", g_strerror(errno));
}

/**
 * Format the pid file name according to the settings specified by the
 * process.
 *
 * @param[out] buf buffer to store the pidfile name
 * @param[in]  buflen size of buf
 **/
static const gchar *
z_process_format_pidfile_name(gchar *buf, gsize buflen)
{
  const gchar *pidfile = process_opts.pidfile;

  if (pidfile == NULL)
    {
      g_snprintf(buf, buflen, "%s/%s.pid", process_opts.pidfile_dir ? process_opts.pidfile_dir : ZORPLIB_PIDFILE_DIR, process_opts.name);
      pidfile = buf;
    }
  else if (pidfile[0] != '/')
    {
      /* complete path to pidfile not specified, assume it is a relative path to pidfile_dir */
      g_snprintf(buf, buflen, "%s/%s", process_opts.pidfile_dir ? process_opts.pidfile_dir : ZORPLIB_PIDFILE_DIR, pidfile);
      pidfile = buf;
      
    }
  return pidfile;
}

/**
 * Write the pid to the pidfile.
 *
 * @param[in] pid pid to write into the pidfile
 **/
static void
z_process_write_pidfile(pid_t pid)
{
  gchar buf[256];
  const gchar *pidfile;
  FILE *fd;
  
  pidfile = z_process_format_pidfile_name(buf, sizeof(buf));
  process_opts.pid_removed = FALSE;
  fd = fopen(pidfile, "w");
  if (fd != NULL)
    {
      fprintf(fd, "%d\n", (int) pid);
      fclose(fd);
    }
  else
    {
      z_process_message("Error creating pid file; file='%s', error='%s'", pidfile, g_strerror(errno));
    }
  
}

/**
 * Read the pid from the pidfile
 *
 * @returns pid in the file or -1 in case of an error
 **/
static pid_t
z_process_read_pidfile(const gchar *pidfile)
{
  FILE *f;
  pid_t pid;
  f = fopen(pidfile, "r");
  if (f)
    {
      if (fscanf(f, "%d", &pid) == 1)
        {
          fclose(f);
          return pid;
        }
      fclose(f);
    }
  return -1;
}

/**
 * Remove the pidfile.
 **/
static void
z_process_remove_pidfile(void)
{
  gchar buf[256];
  const gchar *pidfile;
  pid_t fpid;

  if (process_opts.pid_removed)
    return;

  pidfile = z_process_format_pidfile_name(buf, sizeof(buf));
  
  fpid = z_process_read_pidfile(pidfile);

  if (fpid == -1)
    {
      z_process_message("Error removing pid file; file='%s', error='Could not read pid file'",
          pidfile);
    }
  else if (fpid == getpid())
    {
      if (unlink(pidfile) < 0)
        {
          z_process_message("Error removing pid file; file='%s', error='%s'", pidfile, g_strerror(errno));
        }
      else
        {
          process_opts.pid_removed = TRUE;
        }
    }
}

/**
 * Change the current root to the value specified by the user, causes the
 * startup process to fail if this function returns FALSE. (e.g. the user
 * specified a chroot but we could not change to that directory)
 *
 * @returns TRUE to indicate success
 **/
static gboolean
z_process_change_root(void)
{
  if (process_opts.chroot_dir)
    {
      if (chroot(process_opts.chroot_dir) < 0)
        {
          z_process_message("Error in chroot(); chroot='%s', error='%s'\n", process_opts.chroot_dir, g_strerror(errno));
          return FALSE;
        }
    }
  return TRUE;
}

/**
 * Change the current user/group/groups to the value specified by the user.
 * causes the startup process to fail if this function returns FALSE. (e.g.
 * the user requested the uid/gid to change we could not change to that uid)
 *
 * @returns TRUE to indicate success
 **/
static gboolean
z_process_change_user(void)
{
  uid_t uid = -1;
  gid_t gid = -1;
  
#if HAVE_PRCTL && HAVE_PR_SET_KEEPCAPS
  if (process_opts.caps && !process_opts.nocaps)
    prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);
#endif

  if (process_opts.user && !z_resolve_user(process_opts.user, &uid))
    {
      z_process_message("Error resolving user; user='%s'", process_opts.user);
      return FALSE;
    }

  if (process_opts.group && !z_resolve_group(process_opts.group, &gid))
    {
      z_process_message("Error resolving group; group='%s'", process_opts.group);
      return FALSE;
    }

  if ((gint) gid != -1)
    {
      if (setgid(gid) < 0)
        {
          z_process_message("Error in setgid(); group='%s', error='%s'", process_opts.group, g_strerror(errno));
          if (getuid() == 0)
            return FALSE;
        }
      if (process_opts.user && initgroups(process_opts.user, gid) < 0)
        {
          z_process_message("Error in initgroups(); user='%s', error='%s'", process_opts.user, g_strerror(errno));
          if (getuid() == 0)
            return FALSE;
        }
    }

  if ((gint) uid != -1)
    {
      if (setuid(uid) < 0)
        {
          z_process_message("Error in setuid(); user='%s', error='%s'", process_opts.user, g_strerror(errno));
          if (getuid() == 0)
            return FALSE;
        }
    }
  
  return TRUE;
}

/**
 * Change the current capset to the value specified by the user.  causes the
 * startup process to fail if this function returns FALSE, but we only do
 * this if the capset cannot be parsed, otherwise a failure changing the
 * capabilities will not result in failure
 *
 * @returns TRUE to indicate success
 **/
static gboolean
z_process_change_caps(void)
{
#if ZORPLIB_ENABLE_CAPS
  if (process_opts.caps && !process_opts.nocaps)
    {
      cap_t cap = cap_from_text(process_opts.caps);

      if (cap == NULL)
        {
          z_process_message("Error parsing capabilities: %s", zorp_caps);
          return FALSE;
        }
      else
        {
          if (cap_set_proc(cap) == -1)
            {
              z_process_message("Error setting capabilities; error='%s'", g_strerror(errno));
            }
          cap_free(cap);
        }
      zorp_caps = process_opts.caps;
    }
#endif
  return TRUE;
}

/**
 * Change the current working directory to the value specified by the user
 * and verify that the daemon would be able to dump core to that directory
 * if that is requested.
 **/
static void
z_process_change_dir(void)
{
  const gchar *cwd = NULL;

  if (process_opts.mode != Z_PM_FOREGROUND)
    {
      if (process_opts.cwd)
        cwd = process_opts.cwd;
      else if (process_opts.pidfile_dir)
        cwd = process_opts.pidfile_dir;
      if (cwd)
        IGNORE_UNUSED_RESULT(chdir(cwd));
    }
    
  /* this check is here to avoid having to change directory early in the startup process */
  if ((process_opts.core) && access(".", W_OK) < 0)
    {
      gchar buf[256];
      
      IGNORE_UNUSED_RESULT(getcwd(buf, sizeof(buf)));
      z_process_message("Unable to write to current directory, core dumps will not be generated; dir='%s', error='%s'", buf, g_strerror(errno));
    }
  
}

/**
 * Notify our parent process about the result of the process startup phase.
 *
 * @param[in] ret_num exit code of the process
 *
 * This function is called to notify our parent process (which is the same
 * executable process but separated with a fork()) about the result of the
 * process startup phase. Specifying ret_num == 0 means that everything was
 * dandy, all other values mean that the initialization failed and the
 * parent should exit using ret_num as the exit code. The function behaves
 * differently depending on which process it was called from, determined by
 * the value of the process_kind global variable. In the daemon process it
 * writes to init_result_pipe, in the startup process it writes to the
 * startup_result_pipe.
 *
 * This function can only be called once, further invocations will do nothing.
 **/
static void
z_process_send_result(guint ret_num)
{
  gchar buf[10];
  guint buf_len;
  gint *fd;
  
  if (process_kind == Z_PK_SUPERVISOR)
    fd = &startup_result_pipe[1];
  else if (process_kind == Z_PK_DAEMON)
    fd = &init_result_pipe[1];
  else
    g_assert_not_reached();
    
  if (*fd != -1)
    {
      buf_len = g_snprintf(buf, sizeof(buf), "%d\n", ret_num);
      IGNORE_UNUSED_RESULT(write(*fd, buf, buf_len));
      close(*fd);
      *fd = -1;
    }  
}

/**
 * Retrieves an exit code value from one of the result pipes depending on
 * which process the function was called from. This function can be called
 * only once, further invocations will return non-zero result code.
 *
 * @returns the exit code
 **/
static gint
z_process_recv_result(void)
{
  gchar ret_buf[6];
  gint ret_num = 1;
  gint *fd;
  
  /** @todo FIXME: use a timer */
  if (process_kind == Z_PK_SUPERVISOR)
    fd = &init_result_pipe[0];
  else if (process_kind == Z_PK_STARTUP)
    fd = &startup_result_pipe[0];
  else
    g_assert_not_reached();
  
  if (*fd != -1)
    {
      memset(ret_buf, 0, sizeof(ret_buf));
      if (read(*fd, ret_buf, sizeof(ret_buf)) > 0)
        {
          ret_num = atoi(ret_buf);
        }
      else
        {
          /* the process probably crashed without telling a proper exit code */
          ret_num = 1;
        }
      close(*fd);
      *fd = -1;
    }
  return ret_num;
}

/**
 * This function is the startup process, never returns, the startup process exits here.
 **/
static void
z_process_perform_startup(void)
{
  /* startup process */
  exit(z_process_recv_result());
}


#define SPT_PADCHAR   '\0'

/**
 * Put a new process title in argv[0] (for ps & co.).
 *
 * @param[in] proc_title new process title
 **/
static void
z_process_setproctitle(const gchar* proc_title)
{
  size_t len;

  g_assert(process_opts.argv_start != NULL);
  
  len = g_strlcpy(process_opts.argv_start, proc_title, process_opts.argv_env_len);
  for (; len < process_opts.argv_env_len; ++len)
      process_opts.argv_start[len] = SPT_PADCHAR;
}


#define PROC_TITLE_SPACE 1024
/** Maximum handled restart time */
#define PROC_RESTART_MAX 30

/**
 * Supervise process, returns only in the context of the daemon process, the
 * supervisor process exits here.
 **/
static void
z_process_perform_supervise(void)
{
  pid_t pid;
  gboolean first = TRUE, exited = FALSE;
  gchar proc_title[PROC_TITLE_SPACE];
  size_t restart_time_count = 0;
  time_t restart_time[PROC_RESTART_MAX];
  time_t last_notification_time = 0;
  time_t now, from;
  gint restart_interval_min;
  gint notify_count = 0;

  g_snprintf(proc_title, PROC_TITLE_SPACE, "supervising %s", process_opts.name);
  z_process_setproctitle(proc_title);
  
  /* The value of the checked restarts must be between 2 and PROC_RESTART_MAX.
    At least 2 samples are required for the checking of restarts */
  if (process_opts.restart_max > PROC_RESTART_MAX)
    {
      z_process_message("Warning. The specified value of restart-max parameter is decreaased to '%d'", PROC_RESTART_MAX);
      process_opts.restart_max = PROC_RESTART_MAX;      
  }
  if (process_opts.restart_max < 2)
    {
      z_process_message("Warning. The specified value of restart-max parameter < 2. Changed to default value '5'; restart_max='%d'", process_opts.restart_max);
      process_opts.restart_max = 5;      
    }
  
  /* The supervisor process waits about 1 second before restarts the supervised process also
   * the interval should be at least restart_max * 3 seconds to let a longer period for the checking.
   * The restart itself takes about 3 seconds.
   * Also there is another lower limit, below it the checking may go wrong. The latter value is 5 (currently)
   */
  restart_interval_min = MAX (5, 3 * process_opts.restart_max);
  if (process_opts.restart_interval < restart_interval_min)
    {
      z_process_message("Warning. The specified value of restart-interval parameter < %d. Changed to '%d'; restart_interval='%d'",
                        restart_interval_min, restart_interval_min, process_opts.restart_interval);
      process_opts.restart_interval = 5;      
    }
  while (1)
    {
      gint i = 0;
      gint  restart_count = 1;
      now = time(0);
      from = now - process_opts.restart_interval;

       /* ensure there is at least one free slot */
      if (restart_time_count == PROC_RESTART_MAX)
        {
           memmove(restart_time, restart_time + sizeof(time_t), sizeof(time_t) * (PROC_RESTART_MAX - 1));
           --restart_time_count;
        }

      restart_time[restart_time_count] = time(NULL);
      ++restart_time_count;

      /* checking for restart count in an interval */
      for (i = restart_time_count - 1; i>=0 && restart_time[i] >= from; --i)
        ++restart_count;

      if (pipe(init_result_pipe) != 0)
        {
          z_process_message("Error daemonizing process, cannot open pipe; error='%s'", g_strerror(errno));
          z_process_startup_failed(1, TRUE);
        }
        
      /* fork off a child process */
      if ((pid = fork()) < 0)
        {
          z_process_message("Error forking child process; error='%s'", g_strerror(errno));
          z_process_startup_failed(1, TRUE);
        }
      else if (pid != 0)
        {
          gint rc;
          gboolean deadlock = FALSE;
          
          /* this is the supervisor process */

          /* shut down init_result_pipe write side */
          close(init_result_pipe[1]);
          init_result_pipe[1] = -1;
          
          rc = z_process_recv_result();
          if (first)
            {
              /* first time encounter, we have a chance to report back, do it */
              z_process_send_result(rc);
              if (rc != 0)
                break;
              z_process_detach_stdio();
            }
          first = FALSE;
          if (rc != 0)
            {
              i = 0;
              /* initialization failed in daemon, it will probably exit soon, wait and restart */
              
              while (i < 6 && waitpid(pid, &rc, WNOHANG) == 0)
                {
                  if (i > 3)
                    kill(pid, i > 4 ? SIGKILL : SIGTERM);
                  sleep(1);
                  i++;
                }
              if (i == 6)
                {
                  z_process_message("Initialization failed but the daemon did not exit, even when forced to, trying to recover; pid='%d'", pid);
                  waitpid(pid, &rc, WNOHANG);
                }

              if (restart_count > process_opts.restart_max)
                {
                  z_process_message("Daemon exited but not restarting; restart_count='%d'", restart_count);
                  break;
                }
              else
                {
                  continue;
                }
            }
          
          if (process_opts.check_fn && (process_opts.check_period >= 0))
            {
              i = 1;
              while (!(exited = waitpid(pid, &rc, WNOHANG)))
                {
                  if (i >= process_opts.check_period)
                    {
                      if (!process_opts.check_fn())
                        break;
                      i = 0;
                    }
                  sleep(1);
                  i++;
                }

              if (!exited)
                {
                  gint j = 0;
                  z_process_message("Daemon deadlock detected, killing process;");
                  deadlock = TRUE;
              
                  while (j < 6 && waitpid(pid, &rc, WNOHANG) == 0)
                    {
                      if (j > 3)
                        kill(pid, j > 4 ? SIGKILL : SIGABRT);
                      sleep(1);
                      j++;
                    }
                  if (j == 6)
                    {
                      z_process_message("The daemon did not exit after deadlock, even when forced to, trying to recover; pid='%d'", pid);
                      waitpid(pid, &rc, WNOHANG);
                    }
                }
            }
          else
            {
              waitpid(pid, &rc, 0);
            }

          if (deadlock || WIFSIGNALED(rc) || (WIFEXITED(rc) && WEXITSTATUS(rc) != 0))
            {
              gchar argbuf[128];

              if (!access(Z_PROCESS_FAILURE_NOTIFICATION, R_OK | X_OK)) 
                {
                  const gchar *notify_reason;
                  pid_t npid;
                  gint nrc;
                  
                  now = time(0);

                  if (now - last_notification_time  > process_opts.notify_interval)
                    {
                      last_notification_time = now;
                      npid = fork();
                      switch (npid)
                        {
                        case -1:
                          z_process_message("Could not fork for external notification; reason='%s'", strerror(errno));
                          break;
        
                        case 0:
                          switch(fork())
                            {
                            case -1:
                              z_process_message("Could not fork for external notification; reason='%s'", strerror(errno));
                              exit(1);
                              break;
                            case 0: 
                              if (deadlock)
                                {
                                  notify_reason = "deadlock detected";
                                  argbuf[0] = 0;
                                }
                              else 
                                {
                                  snprintf(argbuf, sizeof(argbuf), "%d; supressed_notifications=%d", WIFSIGNALED(rc) ? WTERMSIG(rc) : WEXITSTATUS(rc), notify_count);
                                  if (WIFSIGNALED(rc))
                                    notify_reason = "signalled";
                                  else
                                    notify_reason = "non-zero exit code";
                                }
                              execlp(Z_PROCESS_FAILURE_NOTIFICATION, Z_PROCESS_FAILURE_NOTIFICATION, 
                                     Z_STRING_SAFE(process_opts.name),
                                     Z_STRING_SAFE(process_opts.chroot_dir),
                                     Z_STRING_SAFE(process_opts.pidfile_dir),
                                     Z_STRING_SAFE(process_opts.pidfile),
                                     Z_STRING_SAFE(process_opts.cwd),
                                     Z_STRING_SAFE(process_opts.caps),
                                     notify_reason,
                                     argbuf,
                                     (deadlock || !WIFSIGNALED(rc) || WTERMSIG(rc) != SIGKILL) ? "restarting" : "not-restarting",
                                     (gchar*) NULL);
                              z_process_message("Could not execute external notification; reason='%s'", strerror(errno));
                              break;
                              
                            default:
                              exit(0);
                              break;
                            } /* child process */
                        default:
                          waitpid(npid, &nrc, 0);
                          break;
                        }
                      notify_count = 0;
                    }
                  else
                    {
                      notify_count++;
                    }
                }
              if (deadlock || !WIFSIGNALED(rc) || WTERMSIG(rc) != SIGKILL)                
                {

                  if (restart_count > process_opts.restart_max)
                    {
                      z_process_message("Daemon exited due to a deadlock/signal/failure, not restarting; exitcode='%d', restart_count='%d'", rc, restart_count);
                      break;
                    }
                  else
                    z_process_message("Daemon exited due to a deadlock/signal/failure, restarting; exitcode='%d'", rc);

                  sleep(1);
                }
              else
                {
                  z_process_message("Daemon was killed, not restarting; exitcode='%d'", rc);
                  break;
                }
            }
          else /* if not (deadlock || WIFSIGNALED(rc) || (WIFEXITED(rc) && WEXITSTATUS(rc) != 0)) */
            {
              z_process_message("Daemon exited gracefully, not restarting; exitcode='%d'", rc);
              break;
            }
        }
      else /* pid == 0 */
        {
          /* this is the daemon process, thus we should return to the caller of z_process_start() */
          /* shut down init_result_pipe read side */
          process_kind = Z_PK_DAEMON;
          close(init_result_pipe[0]);
          init_result_pipe[0] = -1;
          memcpy(process_opts.argv_start, process_opts.argv_orig, process_opts.argv_env_len);
          return;
        }
    }
  exit(0);
}

#if HAVE_SYSTEMD == 1
static gpointer
z_systemd_notify(gpointer user_data)
{
  const gint watchdog_delay = GPOINTER_TO_INT(user_data);

  while (true)
    {
      sleep(watchdog_delay);
      sd_notify(0, "WATCHDOG=1");
    }

  return nullptr;
}
#endif

/**
 * Start the process as directed by the options set by various
 * z_process_set_*() functions.
 **/
void
z_process_start(void)
{
  pid_t pid;
  
  z_process_detach_tty();
  if (process_opts.use_fdlimit_settings)
    z_process_change_limits();
  
  if (process_opts.mode == Z_PM_BACKGROUND)
    {
      /* no supervisor, sends result to startup process directly */
      if (pipe(init_result_pipe) != 0)
        {
          z_process_message("Error daemonizing process, cannot open pipe; error='%s'", g_strerror(errno));
          exit(1);
        }
      
      if ((pid = fork()) < 0)
        {
          z_process_message("Error forking child process; error='%s'", g_strerror(errno));
          exit(1);
        }
      else if (pid != 0)
        {
          /* shut down init_result_pipe write side */
          
          close(init_result_pipe[1]);
          
          /* connect startup_result_pipe with init_result_pipe */
          startup_result_pipe[0] = init_result_pipe[0];
          init_result_pipe[0] = -1;
          
          z_process_perform_startup();
          /* NOTE: never returns */
          g_assert_not_reached();
        }
      process_kind = Z_PK_DAEMON;
      
      /* shut down init_result_pipe read side */
      close(init_result_pipe[0]);
      init_result_pipe[0] = -1;
    }
  else if (process_opts.mode == Z_PM_SAFE_BACKGROUND)
    {
      /* full blown startup/supervisor/daemon */
      if (pipe(startup_result_pipe) != 0)
        {
          z_process_message("Error daemonizing process, cannot open pipe; error='%s'", g_strerror(errno));
          exit(1);
        }
      /* first fork off supervisor process */
      if ((pid = fork()) < 0)
        {
          z_process_message("Error forking child process; error='%s'", g_strerror(errno));
          exit(1);
        }
      else if (pid != 0)
        {
          /* this is the startup process */
          
          /* shut down startup_result_pipe write side */
          close(startup_result_pipe[1]);
          startup_result_pipe[1] = -1;
          
          /* NOTE: never returns */
          z_process_perform_startup();
          g_assert_not_reached();
        }
      /* this is the supervisor process */
      
      /* shut down startup_result_pipe read side */
      close(startup_result_pipe[0]);
      startup_result_pipe[0] = -1;
      
      process_kind = Z_PK_SUPERVISOR;
      z_process_perform_supervise();
      /* we only return in the daamon process here */
    }
  else if (process_opts.mode == Z_PM_FOREGROUND)
    {
      process_kind = Z_PK_DAEMON;
    }
#if HAVE_SYSTEMD == 1
  else if (process_opts.mode == Z_PM_SYSTEMD_NOTIFY)
    {
      const char *watchdog_usec_env_str = getenv("WATCHDOG_USEC");
      if (!watchdog_usec_env_str)
        {
          z_log(nullptr, CORE_ERROR, 0,
                "The systemd-notify process-mode requires the WATCHDOG_USEC environment variable to be set");
          exit(1);
        }

      const int watchdog_usec = atoi(watchdog_usec_env_str);
      const int watchdog_delay = watchdog_usec / (1000000 * 2); // in sec and half of the value
      if (watchdog_delay < 1)
        {
          z_log(nullptr, CORE_ERROR, 0, "WATCHDOG_USEC has invalid value");
          exit(1);
        }

      process_kind = Z_PK_DAEMON;
      z_thread_new("systemd-notify", z_systemd_notify, GINT_TO_POINTER(watchdog_delay));
      sd_notify(0, "READY=1");
    }
#endif
  else
    {
      g_assert_not_reached();
    }
  
  /* NOTE: we need to signal the parent in case of errors from this point. 
   * This is accomplished by writing the appropriate exit code to
   * init_result_pipe, the easiest way doing so is calling z_process_startup_failed.
   * */

  if (!z_process_change_root() ||
      !z_process_change_user() ||
      !z_process_change_caps())
    {
      z_process_startup_failed(1, TRUE);
    }
  z_process_enable_core();

  if (process_opts.mode != Z_PM_SYSTEMD_NOTIFY)
    z_process_change_dir();
}


/**
 * This is a public API function to be called by the user code when
 * initialization failed.
 *
 * @param[in] ret_num exit code
 * @param[in] may_exit whether to exit the process
 **/
void
z_process_startup_failed(guint ret_num, gboolean may_exit)
{
  z_process_send_result(ret_num);
  if (may_exit)
    {
      exit(ret_num);
    }
  else
    {
      z_process_detach_stdio();
    }
}

/**
 * This is a public API function to be called by the user code when
 * initialization was successful, we can report back to the user.
 **/
void
z_process_startup_ok(void)
{
  if (process_opts.mode != Z_PM_SYSTEMD_NOTIFY)
    {
      z_process_write_pidfile(getpid());
    }
  
  z_process_send_result(0);
  z_process_detach_stdio();
}

/**
 * This is a public API function to be called by the user code when the
 * daemon exits after properly initialized (e.g. when it terminates because
 * of SIGTERM). This function currently only removes the PID file.
 **/
void
z_process_finish(void)
{
  if (process_opts.mode != Z_PM_SYSTEMD_NOTIFY)
    {
      z_process_remove_pidfile();
    }
}

/**
 * This is a public API function to be called by the user code when the
 * daemon is about to exit. This function currently only removes the PID file.
 **/
void
z_process_finish_prepare(void)
{
  if (process_opts.mode != Z_PM_SYSTEMD_NOTIFY)
    {
      z_process_remove_pidfile();
    }
}

static gboolean
z_process_process_mode_arg(const gchar *option_name G_GNUC_UNUSED, const gchar *value, gpointer data G_GNUC_UNUSED, GError **error)
{
  if (strcmp(value, "foreground") == 0)
    {
      process_opts.mode = Z_PM_FOREGROUND;
    }
  else if (strcmp(value, "background") == 0)
    {
      process_opts.mode = Z_PM_BACKGROUND;
    }
  else if (strcmp(value, "safe-background") == 0)
    {
      process_opts.mode = Z_PM_SAFE_BACKGROUND;
    }
#if HAVE_SYSTEMD == 1
  else if (strcmp(value, "systemd-notify") == 0)
    {
      process_opts.mode = Z_PM_SYSTEMD_NOTIFY;
    }
#endif
  else
    {
      g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE, "Error parsing process-mode argument");
      return FALSE;
    }
  return TRUE;
}

static GOptionEntry z_process_option_entries[] =
{
  { "foreground",   'F', G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE,     &process_opts.mode,              "Do not go into the background after initialization", NULL },
  { "process-mode",   0,                     0, G_OPTION_ARG_CALLBACK, reinterpret_cast<void *>(z_process_process_mode_arg) ,     "Set process running mode",
#if HAVE_SYSTEMD == 1
    "<foreground|background|safe-background|systemd-notify>" },
#else
    "<foreground|background|safe-background>" },
#endif
  { "user",         'u',                     0, G_OPTION_ARG_STRING,   &process_opts.user,              "Set the user to run as", "<user>" },
  { "uid",            0,  G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,   &process_opts.user,              NULL, NULL },
  { "group",        'g',                     0, G_OPTION_ARG_STRING,   &process_opts.group,             "Set the group to run as", "<group>" },
  { "gid",            0,  G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING,   &process_opts.group,             NULL, NULL },
  { "chroot",       'R',                     0, G_OPTION_ARG_STRING,   &process_opts.chroot_dir,        "Chroot to this directory", "<dir>" },
  { "caps",         'C',                     0, G_OPTION_ARG_STRING,   &process_opts.caps,              "Set default capability set", "<capspec>" },
  { "no-caps",      'N',                     0, G_OPTION_ARG_NONE,     &process_opts.nocaps,            "Disable managing Linux capabilities", NULL },
  { "pidfile",      'P',                     0, G_OPTION_ARG_STRING,   &process_opts.pidfile,           "Set path to pid file", "<pidfile>" },
  { "enable-core",    0,                     0, G_OPTION_ARG_NONE,     &process_opts.core,              "Enable dumping core files", NULL },
  { "fd-limit-min",       0,                  0, G_OPTION_ARG_INT,      &process_opts.fd_limit_min,      "The minimum required number of fds", NULL },
  { "fd-limit-threshold", 0,                  0, G_OPTION_ARG_INT,      &process_opts.fd_limit_threshold,"The required fds per thread (OBSOLETE)", NULL },
  { "restart-max", 0,                         0, G_OPTION_ARG_INT,      &process_opts.restart_max,       "The maximum number of restarts within a specified interval", NULL },
  { "restart-interval", 0,                    0, G_OPTION_ARG_INT,      &process_opts.restart_interval,  "Set the length of the interval in seconds to check process restarts", NULL },
  { "notify_interval", 0,                    0, G_OPTION_ARG_INT,      &process_opts.notify_interval,  "Interval between sending 2 notifications in seconds", NULL },
  { NULL, 0, 0, static_cast<GOptionArg>(0), NULL, NULL, NULL },
};

/**
 * Add the option group (items specified in z_process_option_entries) to the context.
 * 
 * @param[in] ctx context
 **/
void
z_process_add_option_group(GOptionContext *ctx)
{
  GOptionGroup *group;
  
  group = g_option_group_new("process", "Process options", "Process options", NULL, NULL);
  g_option_group_add_entries(group, z_process_option_entries);
  g_option_context_add_group(ctx, group);
}

#endif
