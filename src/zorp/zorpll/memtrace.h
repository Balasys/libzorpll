/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_MEMTRACE_H_INCLUDED
#define ZORP_MEMTRACE_H_INCLUDED

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

void z_mem_trace_init(const gchar *memtrace_file_name);
void z_mem_trace_stats(void);
void z_mem_trace_dump(void);

#if ZORPLIB_ENABLE_MEM_TRACE

#include <stdlib.h>

void *z_malloc(size_t size, gpointer backtrace[]);
void z_free(void *ptr, gpointer backtrace[]);
void *z_realloc(void *ptr, size_t size, gpointer backtrace[]);
void *z_calloc(size_t nmemb, size_t size, gpointer backtrace[]);

#endif

#ifdef __cplusplus
}
#endif

#endif
