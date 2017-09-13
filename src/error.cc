/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#include <glib.h>
#include <zorpll/error.h>

#ifdef G_OS_WIN32
/**
 * Translate Unix error code to Windows error code.
 *
 * @param[in] e Unix error code
 *
 * It's not complete... only contains error codes used somewhere.
 **/
static int
z_errno_translate(int e)
{
  switch (e)
    {
      case EAGAIN: /* a.k.a. EWOULDBLOCK */
      case EINPROGRESS:
        return WSAEWOULDBLOCK;
      case ENOTSOCK:
        return WSAENOTSOCK;
      case EINTR:
        /* WSAEINTR does exist, but has a different meaning */
        return 0;
      default:
        return e;
    }
}
#endif

/**
 * Check if errno/WSAGetLastError() is equal to e, compensating for platform differences.
 *
 * @param[in] e Unix error code
 *
 * @returns TRUE if the compared error values are equivalent.
 **/
gboolean
z_errno_is(int e)
{
#ifdef G_OS_WIN32
  int err;
  
  err = WSAGetLastError();
  if (err == z_errno_translate(e))
    return TRUE;
#endif
  return (errno == e);
}

/**
 * Get the error value, using WSAGetLastError() on Windows and errno elsewhere.
 *
 * @returns the error value
 **/
int
z_errno_get(void)
{
#ifdef G_OS_WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

/**
 * Set the error value, using WSASetLastError() on Windows and errno elsewhere.
 *
 * @param[in] e the error value
 **/
void
z_errno_set(int e)
{
#ifdef G_OS_WIN32
  WSASetLastError(e);
#endif
  errno = e;
}
