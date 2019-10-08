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

#include <zorpll/misc.h>
#include <zorpll/zorplib.h>

#if ZORPLIB_ENABLE_MEM_TRACE

#ifdef G_OS_WIN32
  #include <io.h>
#endif

#ifdef HAVE_DLFCN_H
  #include <dlfcn.h>
#endif
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#ifdef HAVE_UNISTD_H
  #include <unistd.h>
#endif
#include <stdio.h>
#include <time.h>
#include <string.h>

#if HAVE_BACKTRACE
#  include <execinfo.h>
#else
# define backtrace(a, b) {a[0]=NULL}
#endif

#define MEMTRACE_BACKTRACE_LEN 64
#define MEMTRACE_BACKTRACE_BUF_LEN (MEMTRACE_BACKTRACE_LEN * ((sizeof(gpointer) * 2) + 2 + 1) + 1)

#define MEMTRACE_CANARY_SIZE 2
#define MEMTRACE_CANARY_FILL 0xcd
#define MEMTRACE_CANARY_CHECK 0xcdcdcdcd
#define MEMTRACE_CANARY_OVERHEAD sizeof(ZMemTraceCanary) * 2

/**
 * Trace the memory usage of the program using libzorpll
 *
 * @todo FIXME: May we use valgrind instead?
 **/
#define ZORP_ENV_MEMTRACE "ZORP_MEMTRACE"

/**
 * Set some canaries before and after the allocated memory
 * and check it when a pointer deallocated.
 **/
#define ZORP_ENV_MEMTRACE_CANARIES "ZORP_MEMTRACE_CANARIES"

/**
 * Don't deallocate pointers, just fill it with the canaries
 * It's useful when checking for heap corruption
 **/
#define ZORP_ENV_MEMTRACE_HARD     "ZORP_MEMTRACE_HARD"

/**
 * Log all allocation and deallocation events.
 **/
#define ZORP_ENV_MEMTRACE_FULL     "ZORP_MEMTRACE_FULL"

typedef struct _ZMemTraceCanary
{
  gsize size;
  gsize neg_size;
  guint32 canary[MEMTRACE_CANARY_SIZE];
} ZMemTraceCanary;

typedef struct _ZMemTraceEntry
{
  guint32 next;
  gpointer ptr;
  gsize size;
  gpointer backtrace[MEMTRACE_BACKTRACE_LEN];
  gboolean deleted;
} ZMemTraceEntry;

typedef struct _ZMemTraceHead
{
  guint32 list;
  GMutex lock;
  gulong size;
} ZMemTraceHead;

#define MEMTRACE_HASH_SIZE 32768
#define MEMTRACE_HASH_MASK (32767 << 3)
#define MEMTRACE_HASH_SHIFT 3

/**
 * At most this amount of blocks can be allocated at the same time.
 * This preallocates MEMTRACE_HEAP_SIZE * sizeof(ZMemTraceEntry), 
 * which is 268 bytes currently in i386. (17MB)
 **/
#define MEMTRACE_HEAP_SIZE 300000

#define MEMTRACE_TEMP_HEAP_SIZE 65536
/**< If not enough 131072 */

#define MEMTRACE_EOL ((guint32) -1)

ZMemTraceHead mem_trace_hash[MEMTRACE_HASH_SIZE];
ZMemTraceEntry mem_trace_heap[MEMTRACE_HEAP_SIZE];
guint32 mem_trace_free_list = MEMTRACE_EOL;
guint32 mem_block_count = 0, mem_allocated_size = 0, mem_alloc_count = 0;
gboolean mem_trace_initialized = FALSE;
G_LOCK_DEFINE_STATIC(mem_trace_lock);
gchar mem_trace_filename[1024] = ZORPLIB_TEMP_DIR "/zorp-memtrace.log";
gboolean mem_trace_canaries = FALSE;
gboolean mem_trace_hard = FALSE;
gboolean really_trace_malloc = FALSE;

gboolean mem_trace = FALSE;

gchar temp_heap[MEMTRACE_TEMP_HEAP_SIZE];
gint temp_brk = 0;
gint mem_trace_recurse = 0;

#define TMP_ALLOCATED(ptr) (((unsigned int )((char *) ptr - temp_heap)) < MEMTRACE_TEMP_HEAP_SIZE)

void *(*old_malloc)(size_t size);
void (*old_free)(void *ptr);
void *(*old_realloc)(void *ptr, size_t size);
void *(*old_calloc)(size_t nmemb, size_t size);

static void z_mem_trace_printf(const char *format, ...);

/**
 * Save pointers to original free, realloc, calloc and malloc.
 **/
static void
z_mem_trace_init_pointers(void)
{
  dlerror();
  old_free = reinterpret_cast<void (*)(void *)>(dlsym(RTLD_NEXT, "free"));
  if (dlerror() != NULL)
    assert(0);

  old_realloc = reinterpret_cast<void * (*)(void *, size_t)>(dlsym(RTLD_NEXT, "realloc"));
  old_calloc = reinterpret_cast<void * (*)(size_t, size_t)>(dlsym(RTLD_NEXT, "calloc"));
  old_malloc = reinterpret_cast<void *(*)(size_t)>(dlsym(RTLD_NEXT, "malloc"));
}

/**
 * Internal function to initialize memtrace globals.
 *
 * Only the first call will have any effect (unless mem_trace_initialize
 * is somehow unset again).
 **/
static void
z_mem_trace_init_internal(void)
{
  int i;
  
  if (!mem_trace_initialized)
    {
#if defined(__AMD64__) || defined(AMD64) || defined (__x86_64__)
      gpointer temp_buf[10];
#endif /* __amd64__ */

      mem_trace_initialized = TRUE;

#if defined(__AMD64__) || defined(AMD64) || defined (__x86_64__)
      backtrace(temp_buf, 5);
#endif /* __amd64__ */
      
      z_mem_trace_init_pointers();
      
      if (getenv(ZORP_ENV_MEMTRACE))
        {
          mem_trace = TRUE;
          if (getenv(ZORP_ENV_MEMTRACE_CANARIES))
            mem_trace_canaries = TRUE;
      
          if (getenv(ZORP_ENV_MEMTRACE_HARD))
            mem_trace_hard = TRUE;
      
          if (getenv(ZORP_ENV_MEMTRACE_FULL))
            really_trace_malloc = TRUE;
      
          for (i = 0; i < MEMTRACE_HEAP_SIZE; i++)
            {
              mem_trace_heap[i].next = i + 1;
            }
          mem_trace_heap[MEMTRACE_HEAP_SIZE - 1].next = MEMTRACE_EOL;
          mem_trace_free_list = 0;
      
          for (i = 0; i < MEMTRACE_HASH_SIZE; i++) 
            {
              mem_trace_hash[i].list = MEMTRACE_EOL;
              g_mutex_init(&mem_trace_hash[i].lock);
            }
        }
    }
}

/**
 * If the ZORP_ENV_MEMTRACE environment variable says memtrace is needed,
 * set mem_trace and set the trace file name with full path.
 *
 * @param[in] tracefile basename of the trace file.
 **/
void
z_mem_trace_init(const gchar *tracefile_name)
{

  z_mem_trace_init_internal();

  if (tracefile_name && mem_trace)
    {
      g_snprintf(mem_trace_filename, sizeof(mem_trace_filename), ZORPLIB_TEMP_DIR "/%s", tracefile_name);
      z_mem_trace_printf("MemTrace initialized; memtrace='%s', canaries='%s', keep_deleted='%s', full_log='%s'\n",
                YES_NO_STR(mem_trace), YES_NO_STR(mem_trace_canaries), YES_NO_STR(mem_trace_hard), YES_NO_STR(really_trace_malloc));
    }
}

static guint32 
z_mem_trace_hash(gpointer ptr)
{
  return (guint32)((gsize)ptr & MEMTRACE_HASH_MASK) >> MEMTRACE_HASH_SHIFT;
}

#if defined(__i386__)

/**
 * Produce a backtrace from the stack into backtrace.
 *
 * @param[out] backtrace backtrace will be placed here.
 *
 * x86 version.
 **/
void
z_mem_trace_bt(gpointer backt[])
{
  gpointer x;
  gpointer *ebp;
  gint i = 0;

  #if defined(__i386)
    __asm__("mov %%ebp, %0\n": "=r"(ebp));
  #else
  #if defined(__AMD64__) || defined(AMD64) || defined(__x86_64__)
    __asm__("mov %%rbp, %0\n": "=r"(ebp));
  #endif
  #endif

  while ((ebp > &x) && *ebp && *(ebp + 1) && i < MEMTRACE_BACKTRACE_LEN - 1)
    {
      gpointer value = *(ebp + 1);
      
      backt[i] = value;
      i++;
      ebp = reinterpret_cast<gpointer *>(*ebp);
    }
  backt[i] = NULL;
}

#else
#if defined(__AMD64__) || defined(AMD64) || defined(__x86_64__)

void
z_mem_trace_bt(gpointer backt[])
{
  gpointer btrace[MEMTRACE_BACKTRACE_LEN + 1];
  gint i = 0;
  gint length;

  length = backtrace(btrace, MEMTRACE_BACKTRACE_LEN);

  while (btrace[i] && i < length && i < MEMTRACE_BACKTRACE_LEN - 1)
    {
      backt[i] = btrace[i];
      i++;
    }
  backt[i] = NULL;
}

#else
#if defined(__sparc__)

/**
 * Produce a backtrace from the stack into backtrace.
 *
 * @param[out] backt backtrace will be placed here.
 *
 * SPARC version.
 **/
void
z_mem_trace_bt(gpointer backt[])
{
  gpointer x;
  gpointer *fp;
  gint i = 0;

  __asm__("mov %%fp, %0\n": "=r"(fp));

  while ((fp > &x) && *fp && i < MEMTRACE_BACKTRACE_LEN - 1)
    {
      gpointer value = *(fp + 16);
      
      backt[i] = value;
      i++;
      fp = *(fp + 15);
    }
  backt[i] = NULL;
}

#else

/* Stack is not known do dummy backtrace */
/**
 * Produce a backtrace from the stack into backt -- except stack is unknown so this is a dummy.
 *
 * @param[out] backt instead of backtrace, a single NULL will be placed here.
 *
 * Unknown architecture version.
 **/
void
z_mem_trace_bt(gpointer backt[])
{
  backt[0] = NULL;
}
#endif /* __sparc__ */
#endif /* __x86_64__ */
#endif /* __i386__ */

/**
 * Format backtrace into a printable string format.
 *
 * @param[in]  backt the backtrace
 * @param[out] buf buffer to receive the string
 * @param[in]  buflen size of buffer
 *
 * @returns a pointer to the string (same as buf)
 **/
static char *
z_mem_trace_format_bt(gpointer backt[], gchar *buf, guint buflen)
{
  gchar *p = buf;
  gint i, len;

  p[0] = '\0';
  
  for (i = 0; i < MEMTRACE_BACKTRACE_LEN && buflen >= (sizeof(gpointer) * 2) + 3 && backt[i]; i++)
    {
      len = g_snprintf(buf, buflen, "%p,", backt[i]);
      buf += len;
      buflen -= len;
    }
  return p;
}

/**
 * Write a line to the memtrace log if memtrace is enabled.
 *
 * @param[in] format printf-like format string
 * 
 * The format string and the rest of the parameters are expected to be like those of *printf.
 **/
static void
z_mem_trace_printf(const char *format, ...)
{
  gchar buf[1024];
  gint len;
  va_list l;
  gint mem_trace_log_fd = -1;

  if (mem_trace)
    {  
      va_start(l, format);
      len = vsnprintf(buf, sizeof(buf), format, l);
      va_end(l);
      mem_trace_log_fd = open(mem_trace_filename, O_CREAT | O_WRONLY | O_APPEND, 0600);
      if (mem_trace_log_fd != -1)
        {
          write(mem_trace_log_fd, buf, len);
          close(mem_trace_log_fd);
        }
    }
}

/**
 * Log statistics to the memtrace log.
 **/
void
z_mem_trace_stats(void)
{
  z_mem_trace_printf("time: %d, allocs: %ld, blocks: %ld, size: %ld\n", time(NULL), mem_alloc_count, mem_block_count, mem_allocated_size);
}


static gpointer z_mem_trace_check_canaries(gpointer ptr);

void
z_mem_trace_dump()
{
  int i;

  if (mem_trace)
    {
      z_mem_trace_printf("memdump begins\n");
      for (i = 0; i < MEMTRACE_HASH_SIZE; i++)
        {
          ZMemTraceHead *head = &mem_trace_hash[i];
          ZMemTraceEntry *entry;
          guint32 cur;
          
          g_mutex_lock(&head->lock);
          cur = head->list;
          while (cur != MEMTRACE_EOL)
            {
              char backtrace_buf[MEMTRACE_BACKTRACE_BUF_LEN];
              
              entry = &mem_trace_heap[cur];

              z_mem_trace_printf("ptr=%p, size=%d, deleted=%s backtrace=%s\n", entry->ptr, entry->size, entry->deleted ? "TRUE" : "FALSE", z_mem_trace_format_bt(entry->backtrace, backtrace_buf, sizeof(backtrace_buf)));

              if (mem_trace_canaries)
                {
                  z_mem_trace_check_canaries(entry->ptr);
                }
              if (mem_trace_hard && entry->deleted)
                {
                  guint j;
                  for (j = 0; j < entry->size; j++)
                    {
                      if (*((unsigned char *)(entry->ptr) + j) != MEMTRACE_CANARY_FILL)
                        {
                          z_mem_trace_printf("Using freed pointer; ptr=%p\n", entry->ptr);
                        }
                    }
                }
              cur = entry->next;
            }
          g_mutex_unlock(&head->lock);
        }
    }
}

/**
 * Fill areas before and after specified area with canary values.
 *
 * @param[in] ptr raw pointer
 * @param[in] size original size
 *
 * @returns the pointer to be returned
 **/
static gpointer
z_mem_trace_fill_canaries(gpointer ptr, gint size)
{
  if (!ptr)
    return ptr;
  if (mem_trace_canaries)
    {
      ZMemTraceCanary *p_before = (ZMemTraceCanary *) ptr;
      ZMemTraceCanary *p_after = (ZMemTraceCanary *)(((gchar *) ptr) + sizeof(ZMemTraceCanary) + size);
  
      memset(p_before->canary, MEMTRACE_CANARY_FILL, sizeof(p_before->canary));
      memset(p_after->canary, MEMTRACE_CANARY_FILL, sizeof(p_after->canary));
      p_before->size = p_after->size = size;
      p_before->neg_size = p_after->neg_size = -size;
      return (gpointer) (p_before + 1);
    }
  else
    return ptr;
}

/**
 * Aborts the process if the canaries are touched.
 *
 * @param[in] ptr user pointer
 *
 * @returns the pointer to be freed
 **/
static gpointer
z_mem_trace_check_canaries(gpointer ptr)
{
  if (!ptr)
    return ptr;
  if (mem_trace_canaries)
    {
      ZMemTraceCanary *p_before = ((ZMemTraceCanary *) ptr) - 1;
      ZMemTraceCanary *p_after;
      int i;
      
      if (p_before->size != -p_before->neg_size)
        {
          z_mem_trace_printf("Inconsistency in canaries; ptr=%p\n", ptr);
          abort();
        }
      p_after = (ZMemTraceCanary *) (((gchar *) ptr) + p_before->size);
      if (p_after->size != p_before->size ||
          p_after->neg_size != p_before->neg_size)
        {
          z_mem_trace_printf("Inconsistency in canaries; ptr=%p\n", ptr);
          abort();
        }
      for (i = 0; i < MEMTRACE_CANARY_SIZE; i++)
        {
          if (p_before->canary[i] != p_after->canary[i] ||
              p_before->canary[i] != MEMTRACE_CANARY_CHECK)
            {
              z_mem_trace_printf("Touched canary; ptr=%p\n", ptr);
              abort();
            }
        }
      return (gpointer) p_before;
    }
  return ptr;
}

static gboolean
z_mem_trace_add(gpointer ptr, gint size, gpointer backt[])
{
  guint32 hash, new_ndx;
  ZMemTraceEntry *new_entry;
  ZMemTraceHead *head;
  static time_t prev_stats = 0, now;
  
  hash = z_mem_trace_hash(ptr);
  G_LOCK(mem_trace_lock);
  if (mem_trace_free_list == MEMTRACE_EOL)
    {
      G_UNLOCK(mem_trace_lock);
      return FALSE;
    }

  mem_block_count++;
  mem_alloc_count++;
  
  now = time(NULL);
  if (now != prev_stats)
    {
      prev_stats = now;
      z_mem_trace_stats();
    }
  
  mem_allocated_size += size;
  new_ndx = mem_trace_free_list;
  new_entry = &mem_trace_heap[new_ndx];
  mem_trace_free_list = mem_trace_heap[mem_trace_free_list].next;
  
  G_UNLOCK(mem_trace_lock);
  
  new_entry->ptr = ptr;
  new_entry->size = size;
  memmove(new_entry->backtrace, backt, sizeof(new_entry->backtrace));
  head = &mem_trace_hash[hash];
  
  g_mutex_lock(&head->lock);
  
  new_entry->next = head->list;
  head->list = new_ndx;
  
  g_mutex_unlock(&head->lock);

  if (really_trace_malloc)
    {
      gchar buf[1024];
      z_mem_trace_printf("memtrace addblock; ptr='%p', size='%d', bt='%s'\n",
                         ptr, size, z_mem_trace_format_bt(backt, buf, sizeof(buf)));
    }

  return TRUE;
}

static gboolean
z_mem_trace_del(gpointer ptr, gpointer bt[])
{
  guint32 hash, *prev, cur;
  ZMemTraceHead *head;
  ZMemTraceEntry *entry;
  
  hash = z_mem_trace_hash(ptr);
  head = &mem_trace_hash[hash];
  
  g_mutex_lock(&head->lock);
  
  prev = &head->list;
  cur = head->list;
  while (cur != MEMTRACE_EOL && mem_trace_heap[cur].ptr != ptr)
    {
      prev = &mem_trace_heap[cur].next;
      cur = mem_trace_heap[cur].next;
    }
  
  if (cur == MEMTRACE_EOL)
    {
      g_mutex_unlock(&head->lock);
      
      return FALSE;
    }
    
  if (!mem_trace_hard)
    *prev = mem_trace_heap[cur].next;
  g_mutex_unlock(&head->lock);
  
  G_LOCK(mem_trace_lock);
  
  entry = &mem_trace_heap[cur];


  if (really_trace_malloc)
    {
      gchar buf[1024];
      z_mem_trace_printf("memtrace delblock; ptr='%p', size='%d', bt='%s'\n", (void *) entry->ptr, entry->size, z_mem_trace_format_bt(bt, buf, sizeof(buf)));
    }

  if (!mem_trace_hard)
    {
      mem_trace_heap[cur].next = mem_trace_free_list;
      mem_trace_free_list = cur;
      mem_block_count--;
      mem_allocated_size -= mem_trace_heap[cur].size;
    }
  else
    {
      entry->deleted = TRUE;
    }
  G_UNLOCK(mem_trace_lock);
  
  return TRUE;
}

static inline guint32
z_mem_trace_lookup_chain(gpointer ptr, ZMemTraceHead *head)
{
  guint32 cur = MEMTRACE_EOL;
  
  cur = head->list;
  while (cur != MEMTRACE_EOL && mem_trace_heap[cur].ptr != ptr)
    {
      cur = mem_trace_heap[cur].next;
    }
  
  return cur;
}

static int
z_mem_trace_getsize(gpointer ptr)
{
  guint32 hash, cur;
  int size;
  ZMemTraceHead *head;
  
  
  hash = z_mem_trace_hash(ptr);
  head = &mem_trace_hash[hash];
  
  g_mutex_lock(&head->lock);
  cur = z_mem_trace_lookup_chain(ptr, head);
  
  if (cur != MEMTRACE_EOL)
    {
      size = mem_trace_heap[cur].size;
      g_mutex_unlock(&head->lock);
  
      return size;
    }
  
  g_mutex_unlock(&head->lock);
  
  return -1;
}

void *
z_malloc(size_t size, gpointer backt[])
{
  gpointer raw_ptr, user_ptr;
  gchar buf[MEMTRACE_BACKTRACE_BUF_LEN];

  z_mem_trace_init_internal();
  if (old_malloc == NULL)
    {
      raw_ptr = &temp_heap[temp_brk];
      temp_brk += size + mem_trace_canaries * MEMTRACE_CANARY_OVERHEAD;
      if (temp_brk > MEMTRACE_TEMP_HEAP_SIZE)
        {
          temp_brk = 0;
          assert(0);
        }
    }
  else
    raw_ptr = old_malloc(size + mem_trace_canaries * MEMTRACE_CANARY_OVERHEAD);

  if (mem_trace)
    {
      user_ptr = z_mem_trace_fill_canaries(raw_ptr, size);

      if (mem_trace_hard && z_mem_trace_getsize(user_ptr) != -1)
        {
          z_mem_trace_printf("Duplicate memory block; backtrace='%s'\n",
                             z_mem_trace_format_bt(backt, buf, sizeof(buf)));
          abort();
        }

      if (user_ptr && !z_mem_trace_add(user_ptr, size, backt))
        {
          old_free(raw_ptr);
          z_mem_trace_printf("Out of free memory blocks; backtrace='%s'\n",
                             z_mem_trace_format_bt(backt, buf, sizeof(buf)));
          z_mem_trace_stats();
          z_mem_trace_dump();
          return NULL;
        }
    }
  else
    user_ptr = raw_ptr;
  return user_ptr;
}

void
z_free(void *user_ptr, gpointer backt[])
{
  gchar backtrace_buf[MEMTRACE_BACKTRACE_BUF_LEN];
  gpointer raw_ptr;
  gint size;
  
  z_mem_trace_init_internal();
  
  if (mem_trace)
    {
      size = z_mem_trace_getsize(user_ptr);
      if (user_ptr && !z_mem_trace_del(user_ptr, backt))
        {
          z_mem_trace_printf("Trying to free a non-existing memory block; ptr=%p, backtrace='%s'\n",
                             user_ptr, z_mem_trace_format_bt(backt, backtrace_buf, sizeof(backtrace_buf)));
          assert(0);
        }
      raw_ptr = z_mem_trace_check_canaries(user_ptr);

      if (size != -1)
        memset(user_ptr, MEMTRACE_CANARY_FILL, size);
    }
  else
    {
      raw_ptr = user_ptr;
    }

  if (!TMP_ALLOCATED(raw_ptr) && !mem_trace_hard)
    old_free(raw_ptr);
}

void  *
z_realloc(void *user_ptr, size_t size, gpointer backt[])
{
  void *new_ptr, *raw_ptr = NULL;
  size_t old_size = 0;
  gchar buf[MEMTRACE_BACKTRACE_BUF_LEN];
  
  z_mem_trace_init_internal();
  
  if (mem_trace)
    {
      if (user_ptr)
        {
          old_size = z_mem_trace_getsize(user_ptr);
          if (old_size == (size_t) -1 || !z_mem_trace_del(user_ptr, backt))
            {
              z_mem_trace_printf("Trying to realloc a non-existing memory block; ptr=%p, size='%d', info='%s'",
                                 user_ptr, size, z_mem_trace_format_bt(backt, buf, sizeof(buf)));
              assert(0);
            }
          raw_ptr = z_mem_trace_check_canaries(user_ptr);
        }
    }
  else
    {
      raw_ptr = user_ptr;
    }
    
  if (old_realloc && old_malloc)
    {
      if (TMP_ALLOCATED(raw_ptr))
        {
          /* this ptr was allocated on the temp heap, move it to real heap */
          
          z_mem_trace_printf("reallocing space on the temp heap, moving..., ptr=%p, temp_heap=%p, diff=%d, old_size=%d\n",
                             raw_ptr, temp_heap, (char *) raw_ptr-temp_heap, old_size);
          new_ptr = old_malloc(size + mem_trace_canaries * MEMTRACE_CANARY_OVERHEAD);
          if (new_ptr)
            {
              new_ptr = z_mem_trace_fill_canaries(new_ptr, size);
              /* copy user data */
              memmove(new_ptr, user_ptr, old_size);
            }
        }
      else
        {
          if (!mem_trace_hard)
            {
              new_ptr = old_realloc(raw_ptr, size + mem_trace_canaries * MEMTRACE_CANARY_OVERHEAD);
              /* fill_canaries doesn't touch data, only fills the canary info */
              new_ptr = z_mem_trace_fill_canaries(new_ptr, size);
            }
          else
            {
              new_ptr = old_malloc(size + mem_trace_canaries * MEMTRACE_CANARY_OVERHEAD);
              /* fill_canaries doesn't touch data, only fills the canary info */
              new_ptr = z_mem_trace_fill_canaries(new_ptr, size);
              memmove(new_ptr, user_ptr, MIN(size, old_size));
              if (old_size != (size_t) -1)
                memset(user_ptr, MEMTRACE_CANARY_FILL, old_size);
            }
        }
    }
  else
    {
      new_ptr = &temp_heap[temp_brk];
      temp_brk += size + mem_trace_canaries * MEMTRACE_CANARY_OVERHEAD;
      assert(temp_brk < MEMTRACE_TEMP_HEAP_SIZE);
      
      new_ptr = z_mem_trace_fill_canaries(new_ptr, size);
      /* copy user data */
      memmove(new_ptr, user_ptr, old_size);
    }
  if (new_ptr)
    {
      z_mem_trace_add(new_ptr, size, backt);
    }
  return new_ptr;
}

void *
z_calloc(size_t nmemb, size_t size, gpointer backt[])
{
  void *user_ptr, *raw_ptr;

  z_mem_trace_init_internal();
  if (old_calloc == NULL)
    {
      raw_ptr = &temp_heap[temp_brk];
      temp_brk += nmemb * size + mem_trace_canaries * MEMTRACE_CANARY_OVERHEAD;
      assert(temp_brk < MEMTRACE_TEMP_HEAP_SIZE);
    }
  else
    raw_ptr = old_calloc(nmemb, size + mem_trace_canaries * MEMTRACE_CANARY_OVERHEAD);

  if (mem_trace)
    {
      user_ptr = z_mem_trace_fill_canaries(raw_ptr, nmemb * size);
      z_mem_trace_add(user_ptr, nmemb * size, backt);
    }
  else
    {
      user_ptr = raw_ptr;
    }
  return user_ptr;
}

#undef malloc
#undef free
#undef realloc
#undef calloc

/* look up return address */

void *
malloc(size_t size)
{
  gpointer backt[MEMTRACE_BACKTRACE_LEN];

  if (mem_trace)
    z_mem_trace_bt(backt);
  else
    backt[0] = 0;
  return z_malloc(size, backt);
}

void
free(void *ptr)
{
  gpointer backt[MEMTRACE_BACKTRACE_LEN];
  

  if (mem_trace)
    z_mem_trace_bt(backt);
  else
    backt[0] = 0;
  return z_free(ptr, backt);
}

void *
realloc(void *ptr, size_t size)
{
  gpointer backt[MEMTRACE_BACKTRACE_LEN];
  

  if (mem_trace)
    z_mem_trace_bt(backt);
  else
    backt[0] = 0;
  return z_realloc(ptr, size, backt);
}

void *
calloc(size_t nmemb, size_t size)
{
  gpointer backt[MEMTRACE_BACKTRACE_LEN];
  

  if (mem_trace)
    z_mem_trace_bt(backt);
  else
    backt[0] = 0;
  return z_calloc(nmemb, size, backt);
}

#else

/**
 * Save pointers to original free, realloc, calloc and malloc -- dummy version for when memtrace is disabled.
 *
 * @param memtrace_file unused
 **/
void 
z_mem_trace_init(const gchar */* memtrace_file */)
{
}

/**
 * Log statistics to the memtrace log -- dummy version for when memtrace is disabled.
 **/
void 
z_mem_trace_stats(void)
{
}

void 
z_mem_trace_dump()
{
}

#endif
