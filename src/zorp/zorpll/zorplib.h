/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORPLIB_H_INCLUDED
#define ZORPLIB_H_INCLUDED

#if 0
#  ifdef COMPILING_LIBZORPLL
#    define LIBZORPLL_EXTERN __declspec(dllexport)
#  else
#    define LIBZORPLL_EXTERN __declspec(dllimport)
#  endif
#else
#  define LIBZORPLL_EXTERN extern 
#endif

#include <zorpll/zorplibconfig.h>
#include <zorpll/misc.h>

#include <zorpll/memtrace.h>

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CORE_DEBUG      "core.debug"
#define CORE_ERROR      "core.error"
#define CORE_LICENSE    "core.license"
#define CORE_TRACE      "core.trace"
#define CORE_ACCOUNTING "core.accounting"
#define CORE_INFO       "core.info"
#define CORE_STDERR     "core.stderr"
#define CORE_AUTH       "core.auth"
#define CORE_DUMP       "core.dump"
#define CORE_CAPS       "core.caps"
#define CORE_SESSION    "core.session"

#ifdef __cplusplus
}
#endif

#endif
