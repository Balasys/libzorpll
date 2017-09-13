/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_COMPAT_H_INCLUDED
#define ZORP_COMPAT_H_INCLUDED

#if GLIB_CHECK_VERSION(2, 30, 0)
  #define G_LOCK_MUTEX_POINTER(name)  (&(G_LOCK_NAME(name)))
#else
  #define G_LOCK_MUTEX_POINTER(name)  g_static_mutex_get_mutex(&(G_LOCK_NAME(name)))
#endif

#endif /* ZORP_COMPAT_H_INCLUDED */
