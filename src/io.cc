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

#include <zorpll/io.h>
#include <zorpll/log.h>
#include <zorpll/cap.h>
#include <zorpll/error.h>

#include <sys/types.h>
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <fcntl.h>

#ifdef G_OS_WIN32
#  include <winsock2.h>
#else
#  include <netinet/tcp.h>
#  include <netinet/in.h>
#endif

/**
 * This function enables or disables the non-blocking mode of operation on
 * the given fd.
 *
 * @param[in] fd file descriptor to change
 * @param[in] enable specifies whether to enable or disable O_NONBLOCK
 *
 * @returns whether the operation was successful
 **/
gboolean
z_fd_set_nonblock(int fd, gboolean enable)
{
#ifdef G_OS_WIN32

  /* Note: this assumes the fd is a socket. */
  unsigned long argp;
  argp = enable;

  if (!enable)
    {
      if (WSAEventSelect(fd, (WSAEVENT) NULL, 0) == SOCKET_ERROR)
        {
          z_log(NULL, CORE_ERROR, 3, "Disabling socket events failed; fd='%d', error='%08x'",
                fd, z_errno_get());
          return FALSE;
        }
    }

  if (ioctlsocket(fd, FIONBIO, &argp) == SOCKET_ERROR)
    {
      /*LOG
        This message indicates that changing the blocking state on the given fd
        failed for the given reason. Please report this event to the
        Zorp QA team.
       */
      z_log(NULL, CORE_ERROR, 3, "Changing blocking mode failed; fd='%d', enable='%d', error='%08x'",
            fd, enable, z_errno_get());
      return FALSE;
    }
#else
  int flags;

  if ((flags = fcntl(fd, F_GETFL)) == -1)
    return FALSE;

  if (enable)
    flags |= O_NONBLOCK;
  else
    flags &= ~O_NONBLOCK;

  if (fcntl(fd, F_SETFL, flags) < 0)
    {
      /*LOG
        This message indicates that changing the blocking state on the given fd
        failed for the given reason. Please report this event to the
        Zorp QA team.
       */
      z_log(NULL, CORE_ERROR, 3, "Changing blocking mode failed; fd='%d', enable='%d', error='%s'", fd, enable, g_strerror(errno));
      return FALSE;
    }
#endif

  return TRUE;
}

/**
 * This function enables or disables the TCP keepalive feature for socket
 * specified by fd.
 *
 * @param[in] fd file descriptor of a socket
 * @param[in] enable whether to enable or disable TCP keepalive
 *
 * @returns whether the operation was successful
 **/
gboolean
z_fd_set_keepalive(int fd, gboolean enable)
{
  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *)(&enable), sizeof(enable)) == -1)
    {
      /*LOG
        This message indicates that changing the keep-alive state on the given fd
        failed for the given reason. Please report this event to the
        Zorp QA team.
       */
      z_log(NULL, CORE_ERROR, 4, "setsockopt(SOL_SOCKET, SO_KEEPALIVE) failed; fd='%d', enable='%d', error='%s'", fd, enable, g_strerror(errno));
      return FALSE;
    }
  return TRUE;
}

/**
 * This function enables or disables the TCP SO_OOBINLINE feature for socket
 * specified by fd.
 *
 * @param[in] fd file descriptor of a socket
 * @param[in] enable whether to enable or disable TCP SO_OOBINLINE
 *
 * @returns whether the operation was successful
 **/
gboolean
z_fd_set_oobinline(int fd, gboolean enable)
{
  if (setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, (char *)(&enable), sizeof(enable)) == -1)
    {
      /*LOG
        This message indicates that changing the OOBINLINE state on the given fd
        failed for the given reason. Please report this event to the
        Zorp QA team.
       */
      z_log(NULL, CORE_ERROR, 4, "setsockopt(SOL_SOCKET, SO_OOBINLINE) failed; fd='%d', enable='%d', error='%s'", fd, enable, g_strerror(errno));
      return FALSE;
    }
  return TRUE;
}

#ifndef SO_MARK
/* In kernel 2.6.25 the SO_MARK has value 36 if architecture is x86/x86_64
 */
#define SO_MARK 36
#endif

void
z_fd_set_our_mark(int fd, int mark)
{
  int res;
  cap_t saved_caps;
  static gboolean logged = FALSE;

  saved_caps = cap_save();
  cap_enable(CAP_NET_ADMIN);
  res = setsockopt(fd, SOL_SOCKET, SO_MARK, reinterpret_cast<const char *>(const_cast<const int *>(&mark)), sizeof(mark));
  cap_restore(saved_caps);

  if (res == -1 && !logged)
    {
      logged = TRUE;
      /*LOG
        This message indicates that an error occurred while Zorp attempted
        to change the SO_MARK value of the specified fd. This message will
        appear only once and is probably caused by missing kernel support.
        Check your kernel whether it includes support for changing
        MARKs from userspace.
       */
      z_log(NULL, CORE_ERROR, 3, "Error changing MARK; fd='%d', mark='%08x', error='%s'", fd, mark, g_strerror(errno));
    }
}

static int
get_type(int fd)
{
  int type;
  socklen_t len = sizeof(type);

  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&type), &len) != 0)
    {
      /*LOG
        This message indicates that getting socket type on the given fd
        failed for the given reason. Please report this event to the
        Zorp QA team.
       */
      z_log(NULL, CORE_ERROR, 4,
            "getsockopt(SOL_SOCKET, SO_TYPE) failed; fd='%d', error='%s'",
            fd, g_strerror(errno));
      return -1;
    }

  return type;
}

/**
 * This function enables or disables the TCP_NODELAY feature for socket
 * specified by fd. Setting TCP_NODELAY to TRUE disables the Nagle algorithm.
 *
 * @param[in] fd file descriptor of a socket
 * @param[in] enable whether to enable or disable TCP_NODELAY
 *
 * @returns whether the operation was successful
 **/
gboolean
z_fd_set_nodelay(int fd, gboolean enable)
{
  int type = get_type(fd);

  if (type < 0)
    return FALSE;

  if (type != IPPROTO_TCP)
    return TRUE;

  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&enable), sizeof(enable)) == -1)
    {
      /*LOG
        This message indicates that changing the TCP_NODELAY state on the given fd
        failed for the given reason. Please report this event to the
        Zorp QA team.
       */
      z_log(NULL, CORE_ERROR, 4, "setsockopt(IPPROTO_TCP, TCP_NODELAY,) failed; fd='%d', enable='%d', error='%s'",
            fd, enable, g_strerror(errno));
      return FALSE;
    }

  return TRUE;
}

#if ZORPLIB_ENABLE_TOS

void
z_fd_get_peer_tos(gint fd, guint8 *tos)
{
  gint tmp;
  gchar buf[256];
  gboolean tos_found = FALSE;
  socklen_t buflen, len;

  z_enter();
  *tos = 0;
  tmp = 1;
  if (setsockopt(fd, SOL_IP, IP_RECVTOS, &tmp, sizeof(tmp)) < 0)
    {
      z_log(NULL, CORE_ERROR, 8, "Error in setsockopt(SOL_IP, IP_RECVTOS); fd='%d', error='%s'", fd, g_strerror(errno));
      z_leave();
      return;
    }

  buflen = sizeof(buf);
  if (getsockopt(fd, SOL_IP, IP_PKTOPTIONS, &buf, &buflen) >= 0)
    {
      struct msghdr msg;
      struct cmsghdr *cmsg;

      msg.msg_controllen = buflen;
      msg.msg_control = buf;
      for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg))
        {
          if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_TOS)
            {
              *tos = *(guchar *) CMSG_DATA(cmsg);
              tos_found = TRUE;
              break;
            }
        }
    }
  if (!tos_found)
    {
      len = sizeof(*tos);
      if (getsockopt(fd, SOL_IP, IP_TOS, tos, &len) == -1)
        {
          z_log(NULL, CORE_ERROR, 2, "Error in getsockopt(SOL_IP, IP_PKTOPTIONS) || getsockopt(SOL_IP, IP_TOS); fd='%d', error='%s'", fd, g_strerror(errno));
          *tos = 0;
        }
    }
  z_leave();
}

void
z_fd_set_our_tos(gint fd, guint8 tos)
{
  socklen_t len;
#if ZORPLIB_ENABLE_CAPS
  cap_t saved_caps;

  saved_caps = cap_save();
#endif
  len = sizeof(tos);
  cap_enable(CAP_NET_ADMIN);
  if (setsockopt(fd, SOL_IP, IP_TOS, &tos, len) < 0)
    {
      if (errno != ENOTSOCK && errno != EOPNOTSUPP)
        {
          z_log(NULL, CORE_ERROR, 3, "Error setting ToS value on socket; fd='%d', tos='%d', error='%s', errno='%d'", fd, tos, g_strerror(errno), errno);
        }
    }
  else
    {
      z_log(NULL, CORE_DEBUG, 6, "Setting socket ToS value; fd='%d', tos='%d'", fd, tos);
    }
  cap_restore(saved_caps);
}

void
z_fd_get_our_tos(gint fd, guint8 *tos)
{
  socklen_t len;

  *tos = 0;
  len = sizeof(*tos);
  if (getsockopt(fd, SOL_IP, IP_TOS, tos, &len) < 0)
    {
      z_log(NULL, CORE_ERROR, 2, "Error in getsockopt(SOL_IP, IP_TOS); fd='%d', error='%s'", fd, g_strerror(errno));
    }
}
#endif
