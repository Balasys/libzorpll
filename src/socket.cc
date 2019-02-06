/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <zorpll/socket.h>
#include <zorpll/cap.h>
#include <zorpll/log.h>
#include <zorpll/error.h>
#include <zorpll/random.h>

#define MAGIC_FAMILY_NUMBER 111

/**
 * A thin interface around bind() using a ZSockAddr structure for
 * socket address.
 *
 * @param[in] fd socket to bind
 * @param[in] addr address to bind
 *
 * It enables the NET_BIND_SERVICE capability (should be
 * in the permitted set).
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_bind(gint fd, ZSockAddr *addr, guint32 sock_flags)
{
#if ZORPLIB_ENABLE_CAPS
  cap_t saved_caps;
#endif
  GIOStatus rc;

  z_enter();
#if ZORPLIB_ENABLE_CAPS
  saved_caps = cap_save();
#endif
  cap_enable(CAP_NET_BIND_SERVICE); /* for binding low numbered ports */
  cap_enable(CAP_NET_ADMIN); /* for binding non-local interfaces, transparent proxying */

  if (addr->sa_funcs && addr->sa_funcs->sa_bind_prepare)
    addr->sa_funcs->sa_bind_prepare(fd, addr, sock_flags);

  if (addr->sa_funcs && addr->sa_funcs->sa_bind)
    rc = addr->sa_funcs->sa_bind(fd, addr, sock_flags);
  else
    {
      if (addr && z_ll_bind(fd, &addr->sa, addr->salen, sock_flags) < 0)
        {
          gchar buf[MAX_SOCKADDR_STRING];
          /*LOG
            This message indicates that the low level bind failed for the given reason.
           */
          z_log(NULL, CORE_ERROR, 3, "bind() failed; bind='%s', error='%s'", z_sockaddr_format(addr, buf, sizeof(buf)), g_strerror(errno));
          cap_restore(saved_caps);
          z_return(G_IO_STATUS_ERROR);
        }
      rc = G_IO_STATUS_NORMAL;
    }
  cap_restore(saved_caps);
  z_return(rc);
}

/**
 * Accept a connection on the given fd, returning the newfd and the
 * address of the client in a Zorp SockAddr structure.
 *
 * @param[in]  fd accept connection on this socket
 * @param[out] newfd fd of the accepted connection
 * @param[out] addr store the address of the client here
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_accept(gint fd, gint *newfd, ZSockAddr **addr, guint32 sock_flags)
{
  char sabuf[1024];
  socklen_t salen = sizeof(sabuf);
  struct sockaddr *sa = (struct sockaddr *) sabuf;

  /* NOTE: workaround a Linux 2.4.20 kernel problem */
  sa->sa_family = MAGIC_FAMILY_NUMBER;

  do
    {
      *newfd = z_ll_accept(fd, (struct sockaddr *) sabuf, &salen, sock_flags);
    }
  while (*newfd == -1 && z_errno_is(EINTR));
  if (*newfd != -1)
    {
      if (sa->sa_family == MAGIC_FAMILY_NUMBER && salen == sizeof(sabuf))
        {
          /* workaround the linux 2.4.20 problem */
          sa->sa_family = AF_UNIX;
          salen = 2;
        }
      *addr = z_sockaddr_new((struct sockaddr *) sabuf, salen);
    }
  else if (z_errno_is(EAGAIN))
    {
      return G_IO_STATUS_AGAIN;
    }
  else
    {
      /*LOG
        This message indicates that the low level accept of a connection failed
        for the given reason.
       */
      z_log(NULL, CORE_ERROR, 3, "accept() failed; fd='%d', error='%s'", fd, g_strerror(errno));
      return G_IO_STATUS_ERROR;
    }
  return G_IO_STATUS_NORMAL;
}

/**
 * Connect a socket using Zorp style ZSockAddr structure.
 *
 * @param[in] fd socket to connect
 * @param[in] remote remote address
 * @param[in] sock_flags socket flags
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_connect(gint fd, ZSockAddr *remote, guint32 sock_flags)
{
  int rc;

  z_enter();
  do
    {
      rc = z_ll_connect(fd, &remote->sa, remote->salen, sock_flags);
    }
  while (rc == -1 && z_errno_is(EINTR));

  if (rc != -1)
    z_return(G_IO_STATUS_NORMAL);

  if (!z_errno_is(EINPROGRESS))
    {
      int saved_errno = z_errno_get();

      /*LOG
        This message indicates that the low level connection establishment failed for
        the given reason.
        */
      z_log(NULL, CORE_ERROR, 3, "connect() failed; fd='%d', error='%s'", fd, g_strerror(errno));
      z_errno_set(saved_errno);
    }
  z_return(G_IO_STATUS_ERROR);
}

/**
 * Disconnect an already connected socket for protocols that support this
 * operation (for example UDP).
 *
 * @param[in] fd socket to disconnect
 * @param     sock_flags socket flags (not used)
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_disconnect(int fd, guint32 /* sock_flags */)
{
  gint rc;
  struct sockaddr sa;

  z_enter();
  sa.sa_family = AF_UNSPEC;
  do
    {
      rc = connect(fd, &sa, sizeof(struct sockaddr));
    }
  while (rc == -1 && errno == EINTR);

  if (rc != -1)
    z_return(G_IO_STATUS_NORMAL);

  /*LOG
    This message indicates that the low level disconnect failed for
    the given reason.
    */
  z_log(NULL, CORE_ERROR, 3, "Disconnect failed; error='%s'", g_strerror(errno));
  z_return(G_IO_STATUS_ERROR);
}

/**
 * Start listening on this socket given the underlying protocol supports it.
 *
 * @param[in] fd socket to listen
 * @param[in] backlog the number of possible connections in the backlog queue
 * @param     accept_one whether only one connection is to be accepted (used as a hint) (unused)
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_listen(gint fd, gint backlog, guint32 sock_flags)
{
  if (z_ll_listen(fd, backlog, sock_flags) == -1)
    {
      /*LOG
        This message indicates that the low level listen failed for
        the given reason.
       */
      z_log(NULL, CORE_ERROR, 3, "listen() failed; fd='%d', error='%s'", fd, g_strerror(errno));
      return G_IO_STATUS_ERROR;
    }
  return G_IO_STATUS_NORMAL;
}

/**
 * Get the local address where a given socket is bound.
 *
 * @param[in]  fd socket
 * @param[out] local_addr the local address is returned here
 * @param[in]  sock_flags socket flags
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_getsockname(gint fd, ZSockAddr **local_addr, guint32 sock_flags)
{
  char sabuf[1500];
  socklen_t salen = sizeof(sabuf);

  if (z_ll_getsockname(fd, (struct sockaddr *) sabuf, &salen, sock_flags) == -1)
    {
      /*LOG
        This message indicates that the getsockname() system call failed
        for the given fd.
       */
      z_log(NULL, CORE_ERROR, 3, "getsockname() failed; fd='%d', error='%s'", fd, g_strerror(errno));
      return G_IO_STATUS_ERROR;
    }
  *local_addr = z_sockaddr_new((struct sockaddr *) sabuf, salen);
  return G_IO_STATUS_NORMAL;
}

/**
 * Get the remote address where a given socket is connected.
 *
 * @param[in]  fd socket
 * @param[out] peer_addr the address of the peer is returned here
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_getpeername(gint fd, ZSockAddr **peer_addr, guint32 sock_flags)
{
  char sabuf[1500];
  socklen_t salen = sizeof(sabuf);

  if (z_ll_getpeername(fd, (struct sockaddr *) sabuf, &salen, sock_flags) == -1)
    {
      return G_IO_STATUS_ERROR;
    }
  *peer_addr = z_sockaddr_new((struct sockaddr *) sabuf, salen);
  return G_IO_STATUS_NORMAL;
}

/**
 * Get the original destination of a client represented by the socket.
 *
 * @param[in]  fd socket
 * @param[out] dest_addr the address of the peer's original destination is returned here
 * @param[in]  sock_flags socket flags
 *
 * @returns GIOStatus instance
 **/
GIOStatus
z_getdestname(gint fd, ZSockAddr **dest_addr, guint32 sock_flags)
{
  char sabuf[1500];
  socklen_t salen = sizeof(sabuf);

  if (z_ll_getdestname(fd, (struct sockaddr *) sabuf, &salen, sock_flags) == -1)
    {
      return G_IO_STATUS_ERROR;
    }
  *dest_addr = z_sockaddr_new((struct sockaddr *) sabuf, salen);
  return G_IO_STATUS_NORMAL;
}

/* low level functions */

/**
 * Do the actual port binding.
 *
 * @param[in]      fd FD of the socket to bind
 * @param[in, out] sa address to bind to
 * @param[in]      salen length of sa
 * @param[in]      sock_flags flags, see below
 *
 * If ZSF_LOOSE_BIND is enabled in sock_flags, the port will be allocated in the
 * same group ([1, 511], [512, 1023], [1024, 65535]).
 *
 * If ZSF_RANDOM_BIND is _also_ enabled in sock_flags, the port will be allocated
 * randomly in the same group using a cryptographically secure algorithm.
 *
 * @returns 0 on success, -1 on failure (like bind() )
 **/
gint
z_do_ll_bind(int fd, struct sockaddr *sa, socklen_t salen, guint32 sock_flags)
{
  if ((sock_flags & ZSF_LOOSE_BIND) == 0 ||
      sa->sa_family != AF_INET ||
      ntohs(((struct sockaddr_in *) sa)->sin_port) == 0)
    {
      return bind(fd, sa, salen);
    }
  else
    {
      gint rc = -1;

      /* the port is sa is only a hint, the only requirement is that the
       * allocated port is in the same group, but we need to disable REUSE
       * address for this to work properly */

      if ((sock_flags & ZSF_RANDOM_BIND) == 0)  /* in the non-random case, try to bind to the given port */
        rc = bind(fd, sa, salen);

      /* If we're supposed to bind to a random port or we couldn't bind to the specified port,
       * find another in the same group.
       */
      if ((sock_flags & ZSF_RANDOM_BIND) || (rc < 0 && errno == EADDRINUSE))
        {
          gint range;
          gint random_limit; /* stop trying to find an open port randomly after reaching this limit (see below) */
          guint16 port_min, port_max;
          guint16 port;
          guint16 port_mask;    /* for random number bounding */

          port = ntohs(((struct sockaddr_in *) sa)->sin_port);
          if (port < 512)
            {
              port_min = 1;
              port_max = 511;
              port_mask = 0x01ff;
            }
          else if (port < 1024)
            {
              port_min = 512;
              port_max = 1023;
              port_mask = 0x01ff;
            }
          else
            {
              port_min = 1024;
              port_max = 65535;
              port_mask = 0xffff;
            }
          port++;
          range = port_max - port_min + 1;

          /* Try to select port randomly if required.
           * Per the current implementation, if the range is too small, we won't try at all.
           * (none of the above ranges are too small)
           */
          if (sock_flags & ZSF_RANDOM_BIND)
            {
              /* try (range/8) ports randomly */
              for (random_limit = range / 8; random_limit > 0; random_limit--)
                {
                  /* get a random port number within the group. binary bounding won't always work
                   * so it's not used */
                  do
                    {
                      z_random_sequence_get(Z_RANDOM_BASIC, (guchar *) &port, sizeof(port)); /* all ranges are at least 9 bits so we always ask for 16 bits */
                      port &= port_mask;        /* mask bits we don't need */
                      port += port_min;         /* port is normally 0-based, move it into the range */
                          /* NOTE: this may cause some values to be selected with greater probability with some ranges due to wraparound.
                           * the current ranges are unaffected. */
                    }
                  while((port < port_min) || (port > port_max));
                  ((struct sockaddr_in *) sa)->sin_port = htons(port);
                  if (bind(fd, sa, salen) >= 0)
                    {
                      return 0;
                    }
                  else if (errno != EADDRINUSE)
                    {
                      rc = -1;
                      break;
                    }
                }
              /* if it wasn't successful, give up and try to find one sequentially. */
            }

          /* try to find a port sequentially */
          for (; range > 0; port++, range--)
            {
              if (port > port_max)
                port = port_min;
              ((struct sockaddr_in *) sa)->sin_port = htons(port);
              if (bind(fd, sa, salen) >= 0)
                {
                  return 0;
                }
              else if (errno != EADDRINUSE)
                {
                  rc = -1;
                  break;
                }
            }
        }
      return rc;
    }
}

gint
z_do_ll_accept(int fd, struct sockaddr *sa, socklen_t *salen, guint32 /* sock_flags */)
{
  return accept(fd, sa, salen);
}

gint
z_do_ll_connect(int fd, struct sockaddr *sa, socklen_t salen, guint32 /* sock_flags */)
{
  return connect(fd, sa, salen);
}

gint
z_do_ll_listen(int fd, gint backlog, guint32 /* sock_flags */)
{
  return listen(fd, backlog);
}

gint
z_do_ll_getsockname(int fd, struct sockaddr *sa, socklen_t *salen, guint32 /* sock_flags */)
{
  return getsockname(fd, sa, salen);
}

gint
z_do_ll_getpeername(int fd, struct sockaddr *sa, socklen_t *salen, guint32 /* sock_flags */)
{
  return getpeername(fd, sa, salen);
}

/**
 * Table of socket-related functions.
 **/
ZSocketFuncs z_socket_funcs =
{
  z_do_ll_bind,
  z_do_ll_accept,
  z_do_ll_connect,
  z_do_ll_listen,
  z_do_ll_getsockname,
  z_do_ll_getpeername,
  z_do_ll_getsockname
};

ZSocketFuncs *socket_funcs = &z_socket_funcs;

#ifdef G_OS_WIN32

#include <winsock2.h>

gboolean
z_socket_init(void)
{
  WSADATA wsa;
  int res;

  res = WSAStartup(MAKEWORD(2,0), &wsa);
  if (res)
    {
      /*LOG
        This message indicates that Winsock startup failed for the given reason.
       */
      z_log(NULL, CORE_DEBUG, 0, "WinSock startup failed; error_code='%d'", WSAGetLastError() );
      return FALSE;
    }
  return TRUE;
}

void
z_socket_done(void)
{
  WSACleanup();
}

#else

gboolean
z_socket_init(void)
{
  return TRUE;
}

void
z_socket_done(void)
{
}

#endif
