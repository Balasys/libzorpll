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

#include <zorpll/blob.h>
#include <zorpll/log.h>
#include <zorpll/process.h>

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>       

/**
 * @file
 *
 * Some words about the locking semantics of blobs
 *
 * - Blob system needn't be locked, because only the management thread accesses
 *   the enclosed data
 * - Communication between blob and blob system is managed by the async queue of
 *   the blob system:
 *     - pushes itself into the systems async queue
 *     - waits until the management thread notifies it about the completion of
 *       the request by signalling the blobs cond_reply
 **/

/** Temporary buffer size for reading from streams */
#define Z_BLOB_COPY_BUFSIZE     8192

/** Default blob system instance */
ZBlobSystem  *z_blob_system_default = NULL;

/* And its default attributes
 * (Don't worry about the names, they will be used only 3 times...)*/
const gchar *z_blob_system_default_tmpdir = ZORPLIB_TEMP_DIR;     /**< directory to store the blobs in */
gint64 z_blob_system_default_max_disk_usage = 1024*0x100000;      /**< max disk usage = 1 GB */
gsize z_blob_system_default_max_mem_usage = 256*0x100000;         /**< max mem usage = 256 MB */
gsize z_blob_system_default_lowat = 96*0x100000;                  /**< lowat = 96 MB */
gsize z_blob_system_default_hiwat = 128*0x100000;                 /**< hiwat = 128 MB */
gsize z_blob_system_default_noswap_max = 16384;                   /**< noswap_max = 16 kB */

/** local functions of blobs */
static void z_blob_alloc(ZBlob *self, gint64 req_size);

/** Dummy magic pointer to signal that the management thread should exit */
static void Z_BLOB_THREAD_KILL(void)
{
  /* dummy */
}

/** Dummy magic pointer to signal that some memory is freed up and the management thread should check its waiting list */
static void Z_BLOB_MEM_FREED(void)
{
  /* dummy */
}


/**
 * Writes a blob out to disk, called only from z_blob_system_threadproc()
 *
 * @param[in] self this
 *
 * @warning Caller must hold a lock BOTH on the blob AND the blob system!
 **/
static void
z_blob_swap_out(ZBlob *self)
{
  off_t err;
  gssize written, remain;

  z_enter();
  g_assert(self);
  if (!self->storage_locked && !self->is_in_file && self->system)
    {
      err = lseek(self->fd, 0, SEEK_SET);
      if (err < 0)
        {
          z_log(NULL, CORE_ERROR, 0, "Blob error, lseek() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
          g_assert(0);
        }
      remain = self->size;
      while (remain > 0)
        {
          written = write(self->fd, self->data, remain);
          if (written < 0)
            {
              if (errno == EINTR)
                {
                  continue;
                }
              else
                {
                  z_log(NULL, CORE_ERROR, 0, "Blob error, write() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
                  g_assert(0);
                }
            }
          remain -= written;
        }
      self->is_in_file = 1;
      g_free(self->data);
      self->data = NULL;
      self->stat.swap_count++;
      self->stat.last_accessed = time(NULL);
      self->system->mem_used -= self->alloc_size;
      self->system->disk_used += self->size;
      self->alloc_size = self->size;
    }
  z_return();
}

/**
 * Signal the completion of a request. Called only from z_blob_system_threadproc().
 *
 * @param[in] self this
 **/
static void
z_blob_signal_ready(ZBlob *self)
{
  g_mutex_lock(&self->mtx_reply);
  g_cond_signal(&self->cond_reply);
  self->replied = TRUE;
  g_mutex_unlock(&self->mtx_reply);
}

/**
 * Checks if a blob may allocate self->alloc_req additional bytes.
 *
 * @param[in] self this
 *
 * @returns TRUE if req granted, FALSE if denied
 **/
static gboolean 
z_blob_check_alloc(ZBlob *self)
{
  gsize         disk_available, mem_available;
  gsize         req_total;
  gboolean      success = FALSE, on_disk = FALSE;

  mem_available = self->system->mem_max - self->system->mem_used;
  disk_available = self->system->disk_max - self->system->disk_used;
  req_total = self->alloc_size + self->alloc_req;

  /* FIXME: Need more sophisticated way to allocating.
   *
   * 1. blob is in memory     free space in memory                   => allocate in memory
   * 2.         in memory                in disk     !storage_locked => swap out + allocate
   * 3.         in memory                in disk      storage_locked => ???
   * 4.         in disk                  in memory   !storage locked => swap in (maybe)
   * 5.         in disk                  in memory    storage locked => ???
   * 6.         in disk                  in disk                     => allocate in disk
   *
   * But in this function not all situation handled. (I think.)
   */
  if (self->is_in_file)
    {
      self->system->disk_used += self->alloc_req;
      success = TRUE;
      on_disk = TRUE;
    }
  else if ((self->alloc_req < 0) || ((gsize)self->alloc_req <= mem_available))
    {
      self->system->mem_used += self->alloc_req;
      success = TRUE;
      on_disk = FALSE;
    }
  else if (!self->storage_locked && (req_total <= disk_available)) /* don't fit in mem but fits on disk */
    {
      /* FIXME: !!! FOR TESTING ONLY !!!
       * The current swapping policy is definitely quite lame, it must
       * be replaced with some more intelligent algorithm.
       * Now, if the blob can't be kept in memory, it goes directly to disk.
       * Then, if any self gets freed, we try to find the most appropriate
       * one on disk that would fit in the available ram, and fetch it in.
       * The decision factor is the number of accesses divided by the
       * time elapsed since the last access. Ummm, it cries for some
       * refinement, but should work for now :)...
       */
      z_log(NULL, CORE_DEBUG, 7, "Blob does not fit, swapping out; self_size='%" G_GINT64_FORMAT "'", self->size);
      z_blob_swap_out(self);
      self->system->disk_used += self->alloc_req;
      success = TRUE;
      on_disk = TRUE;
    }
  else if (req_total < (disk_available + mem_available)) /* we have the space for it, but partly on disk */
    {
      /* Premise: if we had anything to fetch in, it would have been already
       * done. (Not quite true, best-fit candidate is fetched in. There may
       * be a better alignment, but finding it is an NP-strong problem -
       * analogous to the 'backpack' problem).
       * Now, treat this case as if there weren't enough space...*/
      ;
    }
  else /* impossible to allocate */
    {
      ;
    }

  if (self->alloc_req < 0)
    g_async_queue_push(self->system->req_queue, reinterpret_cast<gpointer>(Z_BLOB_MEM_FREED));

  z_log(NULL, CORE_DEBUG, 7, "Blob allocation result; result='%s', store='%s', requested_size='%" G_GSSIZE_FORMAT "', mem_avail='%" G_GSIZE_FORMAT "', disk_avail='%" G_GSIZE_FORMAT "'", 
        success ? "granted" : "denied", on_disk ? "disk" : "mem", req_total, mem_available, disk_available);

  return success;
}

/**
 * Try to fetch in blobs if there is enough space.
 *
 * @param[in] self this
 *
 * Try to fetch in blobs if there is enough space for that.
 * Called only from threadproc(), so exclusive access to all memory and blob
 * management data is implicitly granted.
 **/
void
z_blob_system_swap_in(ZBlobSystem *self)
{
  gint64        space_available;
  gdouble       dec_factor, dec_factor_best;
  GList         *cur;
  ZBlob         *blob, *best;
  time_t        now, elapsed;
  gint          swap_count;
  gint64        swap_bytes;
  off_t         err;
  gssize        rd;


  /**
   * @todo FIXME: find and define a better swap algorithm and implement it
   * in a less scattered way 
   **/

  /**
   * Swap-in algorithm is as follows:
   *  - memory store is preferred
   *  - when the amount in RAM is less than lowat AND the amount on
   *    disk is more than hiwat, swap-in is started
   *  - the best blob is selected and it is swapped in  
   **/

  if (self->mem_used >= self->lowat || self->disk_used < self->hiwat)
    return;

  z_log(NULL, CORE_DEBUG, 7, "Starting blob swap-in; mem_used='%" G_GSIZE_FORMAT "', disk_used='%" G_GINT64_FORMAT "', lowat='%" G_GSIZE_FORMAT "'",
        self->mem_used, self->disk_used, self->lowat);
  swap_count = 0;
  swap_bytes = 0;
  do
    {
      time(&now);
      space_available = self->hiwat - self->mem_used;
      dec_factor_best = -1;
      best = NULL;

      for (cur = self->blobs; cur; cur = cur->next)
        {
          blob = (ZBlob *) cur->data;

          if (z_blob_lock(blob, 0)) /* zero timeout -> trylock */
            {
              if (!blob->storage_locked && blob->is_in_file && (blob->alloc_size <= space_available))
                {
                  elapsed = now - blob->stat.last_accessed;
                  dec_factor = (elapsed > 0) ? (blob->stat.req_rd + blob->stat.req_wr) / elapsed : 0;
                  if (dec_factor > dec_factor_best)
                    {
                      dec_factor_best = dec_factor;
                      best = blob;
                    }
                }
              z_blob_unlock(blob);
            }
        }

      if (best)
        {
          z_log(NULL, CORE_DEBUG, 8, "Swapping in blob; blob_size='%" G_GINT64_FORMAT "'", best->size);
          if (z_blob_lock(best, 0)) /* zero timeout -> trylock */
            {
              if (!best->storage_locked && best->is_in_file && (best->alloc_size <= space_available))
                {
                  gssize remain;
                  err = lseek(best->fd, 0, SEEK_SET);
                  if (err == (off_t)-1)
                    {
                      z_log(NULL, CORE_ERROR, 0, "Blob error, lseek() failed; file='%s', error='%s'", best->filename, g_strerror(errno));
                      g_assert(0);
                    }
                  best->data = g_new0(gchar, best->alloc_size);
                  
                  remain = best->size;
                  while (remain > 0)
                    {
                      rd = read(best->fd, best->data, remain);
                      if (rd < 0)
                        {
                          if (errno == EINTR)
                            {
                              continue;
                            }
                          else
                            {
                              z_log(NULL, CORE_ERROR, 0, "Blob error, read() failed; file='%s', error='%s'", best->filename, g_strerror(errno));
                              g_assert(0);
                            }
                        }
                      else if (rd == 0)
                        break;
                      
                      remain -= rd;
                    }

                  best->is_in_file = 0;
                  err = ftruncate(best->fd, 0);
                  if (err < 0)
                    z_log(NULL, CORE_DEBUG, 7, "Blob error, ftruncate() failed; file='%s', error='%s'", best->filename, g_strerror(errno));
                  best->stat.last_accessed = time(NULL);
                  best->system->disk_used -= best->alloc_size;
                  best->system->mem_used += best->alloc_size;
                  swap_count++;
                  swap_bytes += best->size;
                }
              z_blob_unlock(best);
            }
        }
    } 
  while (best);
  z_log(NULL, CORE_INFO, 5, "Blob swap-in complete; swap_count='%d', swap_bytes='%" G_GINT64_FORMAT "'", swap_count, swap_bytes);
}

/**
 * Report disk and memory usage and other statistics on a ZBlobSystem to the log.
 *
 * @param[in] self this
 **/
void
z_blob_system_report_usage(ZBlobSystem *self)
{
   /**
    * @todo FIXME:
    *
    * - Prettier format
    * - Counter for blobs in disk and blobs in memory
    *
    * - Average/min/max blob size
    * - Average/min/max blob lifetime
    * - Average/min/max moving count (to/from swap) of blobs.
    * - Average/min/max length of waiting queue.
    *
    **/
   z_log(NULL, CORE_INFO, 4, "Blob system usage: Disk used: %" G_GINT64_FORMAT " from %" G_GINT64_FORMAT ". Mem used: %" G_GSIZE_FORMAT " from %" G_GSIZE_FORMAT ". Blobs in use: %d. Waiting queue length: (cur/max/min/avg) %d/%d/%d/%d",
                             self->disk_used, self->disk_max,
                             self->mem_used, self->mem_max,
                             g_list_length(self->blobs),
                             g_list_length(self->waiting_list), -1, -1, -1);
}

/**
 * Thread procedure of ZBlobSystem.
 *
 * @param[in] self this
 *
 * Performs the swapping/storage maintenance tasks described in the spec.
 *
 * @returns Currently just self
 **/
static gpointer
z_blob_system_threadproc(ZBlobSystem *self)
{
  ZBlob *blob;
  GList *cur, *del;
  gssize blob_alloc_req;
  GTimeVal next_time, now;
  glong interval = 300; /* 5 min. */

  z_enter();
  g_assert(self);
  g_mutex_lock(&self->mtx_blobsys);
  g_cond_signal(&self->cond_thread_started);
  g_mutex_unlock(&self->mtx_blobsys);

  g_get_current_time(&next_time);
  next_time.tv_sec += interval;

  while (1)
    {
      #if GLIB_MINOR_VERSION >= 32
        blob = static_cast<ZBlob *>(g_async_queue_timeout_pop(self->req_queue, interval * 1000000));   /* blocks until there is a requesting blob in the queue */
      #else
        blob = g_async_queue_timed_pop(self->req_queue, &next_time);   /* blocks until there is a requesting blob in the queue */
      #endif
      
      if (blob == NULL)
        {
          g_get_current_time(&next_time);
          next_time.tv_sec += interval;
          g_mutex_lock(&self->mtx_blobsys);
          z_blob_system_report_usage(self);
          g_mutex_unlock(&self->mtx_blobsys);
          continue;
        }
      
      g_get_current_time(&now);

      if (now.tv_sec > next_time.tv_sec)
        {
          g_mutex_lock(&self->mtx_blobsys);
          z_blob_system_report_usage(self);
          g_mutex_unlock(&self->mtx_blobsys);
        }
      
      if (blob == (ZBlob*)Z_BLOB_THREAD_KILL)
        break;

      g_mutex_lock(&self->mtx_blobsys);
      if (blob == (ZBlob*)Z_BLOB_MEM_FREED)
        {
          /* check the waiting queue - it is enough to check on successful negative alloc requests,
           * because this is the only case when memory is freed up */
          cur = self->waiting_list;
          while (cur)
            {
              blob = (ZBlob*) cur->data;
              del = NULL;
              blob->approved = z_blob_check_alloc(blob);
              if (blob->approved)
                {
                  del = cur;
                  z_blob_signal_ready(blob);
                }
              cur = cur->next;
              if (del)
                self->waiting_list = g_list_delete_link(self->waiting_list, del);
            }

          /* try to swap in blobs - makes sence only on negative alloc reqs, too */
          z_blob_system_swap_in(self);
        }
      else
        {
          blob_alloc_req = blob->alloc_req;
          blob->approved = z_blob_check_alloc(blob);

          if (!blob->approved) /* In case of denial, move the blob to the waiting queue */
            {
              z_log(NULL, CORE_INFO, 4, "Blob storage is full, adding allocate request to the waiting list; size='%" G_GSIZE_FORMAT "'", blob_alloc_req);
              self->waiting_list = g_list_append(self->waiting_list, blob);
            }
          else  /* send back the result to the blob */
            {
              z_blob_signal_ready(blob);
            }
        }
      g_mutex_unlock(&self->mtx_blobsys);
    }
  z_leave();
  g_thread_exit(self);
  z_return(self);
}

/**
 * Initialize the default blob system.
 **/
void
z_blob_system_default_init(void)
{
  z_enter();
  z_blob_system_default = z_blob_system_new(z_blob_system_default_tmpdir,
                                            z_blob_system_default_max_disk_usage,
                                            z_blob_system_default_max_mem_usage,
                                            z_blob_system_default_lowat,
                                            z_blob_system_default_hiwat,
                                            z_blob_system_default_noswap_max);
  z_return();
}

/**
 * Destroy the default blob system.
 **/
void
z_blob_system_default_destroy(void)
{
  z_enter();
  if (z_blob_system_default)
    {
      z_blob_system_unref(z_blob_system_default);
      z_blob_system_default = NULL;
    }
  z_return();
}

/**
 * Create a new blob system using the given parameters.
 *
 * @param[in] dir directory to put the swapped blobs into
 * @param[in] dmax max disk usage size
 * @param[in] mmax max mem usage size
 * @param[in] low low water mark
 * @param[in] hiw high water mark
 * @param[in] nosw maximal size that wont't be swapped
 *
 * @returns The new blob system instance
 **/
ZBlobSystem* 
z_blob_system_new(const char *dir, gint64 dmax, gsize mmax, gsize low, gsize hiw, gsize nosw)
{
  ZBlobSystem   *self;

  z_enter();
  self = g_new0(ZBlobSystem, 1);

  z_refcount_set(&self->ref_cnt, 1);
  self->dir = strdup(dir);
  self->disk_max = dmax;
  self->mem_max = mmax;
  self->disk_used = self->mem_used = 0;
  if (mmax <= low)
      low = mmax - 1;
  self->lowat = low;
  if (mmax <= hiw)
      hiw = mmax - 1;
  self->hiwat = hiw;
  self->noswap_max = nosw;
  self->blobs = NULL;
  g_mutex_init(&self->mtx_blobsys);
  g_cond_init(&self->cond_thread_started);
  self->req_queue = g_async_queue_new();
  self->waiting_list = NULL;

  g_mutex_lock(&self->mtx_blobsys);
  self->thr_management = g_thread_new(NULL, (GThreadFunc)z_blob_system_threadproc,
                              (gpointer)self);
  g_cond_wait(&self->cond_thread_started, &self->mtx_blobsys);
  g_mutex_unlock(&self->mtx_blobsys);
  self->active = TRUE;
  z_return(self);
}

/**
 * Increase reference count of a blob system.
 *
 * @param[in] self the blob system object
 **/
void
z_blob_system_ref(ZBlobSystem *self)
{
  z_enter();
  z_refcount_inc(&self->ref_cnt);
  z_return();
}

/**
 * Decrease reference count of a blob system; destroy it if the count reaches zero.
 *
 * @param[in] self the blob system object
 *
 * This function decreases the reference count of the blob system
 * object given. If the reference count reaches zero, the blob system
 * will be destroyed. If there were pending requests in a to-be-destroyed
 * blob system, this fact will be logged.
 **/
void
z_blob_system_unref(ZBlobSystem *self)
{
  ZBlob *blob;
  GList *cur, *next;
  gint n;

  z_enter();
  g_assert(self); 
  if (z_refcount_dec(&self->ref_cnt))
    {
      self->active = FALSE;
      /** @todo FIXME: itt lockolni kell */
      g_async_queue_push(self->req_queue, reinterpret_cast<gpointer>(Z_BLOB_THREAD_KILL));
      g_thread_join(self->thr_management);

      n = 0;
      for (cur = self->waiting_list; cur; cur = next)
        {
          next = cur->next;
          blob = (ZBlob*) cur->data;
          blob->approved = FALSE;
          z_blob_signal_ready(blob);
          self->waiting_list = g_list_delete_link(self->waiting_list, cur);
          n++;
        }
      if (n)
        z_log(NULL, CORE_INFO, 5, "Pending requests found for a to-be-destroyed blob system; num_requests='%d'", n);

      n = 0;
      for (cur = self->blobs; cur; cur = next)
        {
          next = cur->next;
          blob = (ZBlob*)cur->data;
          z_blob_unref(blob);
          n++;
        }
      if (n)
        z_log(NULL, CORE_INFO, 5, "Active blobs found in a to-be-destroyed blob system; num_blobs='%d'", n);

      if (self->dir)
        g_free(self->dir);
      if (g_mutex_trylock(&self->mtx_blobsys))
        {
          g_mutex_unlock(&self->mtx_blobsys);
          g_mutex_clear(&self->mtx_blobsys);
        }
      else
        {
          /* Some blob operations are in progress: z_blob_new, _unref, _alloc, _get_file */
        }
      g_cond_clear(&self->cond_thread_started);
      g_async_queue_unref(self->req_queue);
      g_list_free(self->waiting_list);
      g_free(self);
    }
  z_return();
}



/******************************************************************************
 * ZBlobStatistic
 ******************************************************************************/
/**
 * Initialize a ZBlobStatistic instance.
 *
 * @param[in] self this
 **/
void
z_blob_statistic_init(ZBlobStatistic *self)
{
  g_assert(self);
  self->req_rd = self->req_wr = self->swap_count = self->alloc_count = 0;
  self->total_rd = self->total_wr = 0;
  self->created = self->last_accessed = time(NULL);
}

/******************************************************************************
 * ZBlob
 ******************************************************************************/


/**
 * Create a new blob.
 *
 * @param[in] sys Blob system to create the blob into
 * @param[in] initial_size Initial size to allocate.
 *
 * This function creates a new blob. If sys is NULL, z_blob_system_default will be used.
 *
 * @returns The new blob instance
 **/
ZBlob*
z_blob_new(ZBlobSystem *sys, gsize initial_size)
{
  ZBlob   *self;

  z_enter();
  if (!sys)
    sys = z_blob_system_default;

  if (!sys || !sys->active) 
    z_return(NULL);

  self = g_new0(ZBlob, 1);
  self->system = sys;

  self->filename = g_strdup_printf("%s/blob_XXXXXX", self->system->dir);
  self->fd = mkstemp(self->filename);

  if (self->fd < 0)
    {
      z_log(NULL, CORE_ERROR, 2, "Error creating blob file: file='%s', error='%s'", self->filename, strerror(errno));
      g_free(self->filename);
      g_free(self);
      z_return(NULL);
    }
  
  z_refcount_set(&self->ref_cnt, 1);
  self->size = 0;
  self->alloc_size = 0;
  self->data = NULL;
  self->is_in_file = FALSE;
  g_mutex_init(&self->mtx_reply);
  g_cond_init(&self->cond_reply);
  self->mapped_ptr = NULL;
  self->mapped_length = 0;
  self->storage_locked = FALSE;
  self->replied = FALSE;

  z_blob_statistic_init(&self->stat);
  g_mutex_init(&self->mtx_lock);

  g_mutex_lock(&self->system->mtx_blobsys);
  self->system->blobs = g_list_append(self->system->blobs, self);
  g_mutex_unlock(&self->system->mtx_blobsys);

  if (initial_size > 0)
    z_blob_alloc(self, initial_size);
  z_return(self);
}

/**
 * Increase reference count of blob and return a reference to it.
 *
 * @param[in] self this
 *
 * @returns self
 **/
ZBlob *
z_blob_ref(ZBlob *self)
{
  z_enter();
  z_refcount_inc(&self->ref_cnt);
  z_return(self);
}

/**
 * Decrease reference count of blob; destroy it if the count reaches zero.
 *
 * @param[in] self this
 **/
void
z_blob_unref(ZBlob *self)
{
  z_enter();
  if (self && z_refcount_dec(&self->ref_cnt))
    {
      g_mutex_lock(&self->system->mtx_blobsys);
      self->alloc_req = -self->alloc_size;
      self->system->blobs = g_list_remove(self->system->blobs, self);
      z_blob_check_alloc(self);
      g_mutex_unlock(&self->system->mtx_blobsys);

      if (self->data)
        g_free(self->data);

      if (self->fd >= 0)
        close(self->fd);

      if (self->filename)
        {
          if (unlink(self->filename))
            z_log(NULL, CORE_ERROR, 3, "Error removing blob file, unlink() failed; file='%s', error='%s'", self->filename, strerror(errno));
          g_free(self->filename);
          self->filename = NULL;
        }

      g_mutex_clear(&self->mtx_reply);
      g_cond_clear(&self->cond_reply);
      if (g_mutex_trylock(&self->mtx_lock))
        {
          g_mutex_unlock(&self->mtx_lock);
          g_mutex_clear(&self->mtx_lock);
        }
      else
        {
          z_log(NULL, CORE_ERROR, 3, "Error while destroying blob, someone still has a lock on it;");
          /* someone has locked the blob by z_blob_get_file or _get_ptr, and forgot to release it */
        }
      g_free(self);
    }
  z_return();
}

/**
 * Lock a blob.
 *
 * @param[in] self this
 * @param[in] timeout Timeout for locking. A negative value means infinite and thus blocking mode. Zero means nonblocking mode.
 *
 * @returns TRUE if successfully locked.
 **/
gboolean
z_blob_lock(ZBlob *self, gint timeout)
{
  gboolean        res;
  struct timeval  tvnow, tvfinish;

  z_enter();
  g_assert(self);

  if (timeout < 0)        /* infinite timeout -> blocking mode */
    {
      g_mutex_lock(&self->mtx_lock);
      res = TRUE;
    }
  else if (timeout == 0)  /* zero timeout -> nonblocking mode */
    {
      res = g_mutex_trylock(&self->mtx_lock);
    }
  else                    /* positive timeout */
    {
      gettimeofday(&tvfinish, NULL);
      tvfinish.tv_sec += (timeout / 1000);
      tvfinish.tv_usec += 1000 * (timeout % 1000);
      tvfinish.tv_sec += (tvfinish.tv_usec / 1000000);
      tvfinish.tv_usec %= 1000000;
      /* FIXME: maybe g_cond_wait_timed_wait ? */
      do
        {
          res = FALSE;
          if (g_mutex_trylock(&self->mtx_lock))
            {
              res = TRUE;
              break;
            }
          usleep(1000);
          gettimeofday(&tvnow, NULL);
        } 
      while ((tvnow.tv_sec < tvfinish.tv_sec) ||
             ((tvnow.tv_sec == tvfinish.tv_sec) && (tvnow.tv_usec < tvfinish.tv_usec)));
    }
  z_return(res);
}

/**
 * Unlock a blob.
 *
 * @param self[in] this
 **/
void
z_blob_unlock(ZBlob *self)
{
  z_enter();
  g_assert(self);
  g_mutex_unlock(&self->mtx_lock);
  z_return();
}

static inline gint64
z_blob_calculate_allocation_size(gint64 req_size, gint64 allocated, gboolean in_file)
{
  gint64 req_alloc_size;

  /* determine the allocation size */
  if ((allocated <= 0) || in_file)
    {
      req_alloc_size = req_size;
    }
  else
    {
      /* First run (if shrinking reqd): go just below the requested size */
      req_alloc_size = allocated;
      while (req_alloc_size > req_size)
        {
          req_alloc_size >>= 1;
        }

      /* Second run: find next available size */  
      while (req_alloc_size < req_size)
        {
          req_alloc_size <<= 1;
        }
    }

  return req_alloc_size;
}

/**
 * Allocate space for the blob (not necessarily in memory!)
 *
 * @param[in] self this
 * @param[in] req_size required space
 *
 * @warning Caller shall hold a write lock on the blob!
 **/
static void
z_blob_alloc(ZBlob *self, gint64 req_size)
{
  gchar         *newdata;
  gint          err;
  gint64        alloc_req;
  gboolean      alloc_granted;

  z_enter();
  g_assert(self);
  g_assert(req_size >= 0);

  /* just return if the allocation needn't change */
  if (z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file) == self->alloc_size)
    z_return();

  alloc_req = z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file) - self->alloc_size;
  g_mutex_lock(&self->system->mtx_blobsys);
  self->alloc_req = alloc_req;
  alloc_granted = z_blob_check_alloc(self);
  g_mutex_unlock(&self->system->mtx_blobsys);
  if (!alloc_granted)
    {
      self->approved = FALSE;
      g_mutex_lock(&self->mtx_reply);
      g_async_queue_push(self->system->req_queue, self);
      while (!self->replied)
        g_cond_wait(&self->cond_reply, &self->mtx_reply);
      self->replied = FALSE;
      g_mutex_unlock(&self->mtx_reply);
      alloc_granted = self->approved;
    }

  g_assert(alloc_granted);

  if (self->is_in_file)
    {
      err = ftruncate(self->fd, z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file));
      if (err < 0)
        z_log(NULL, CORE_ERROR, 3, "Error truncating blob file, ftruncate() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
    }
  else
    {
      newdata = g_renew(gchar,
                        self->data,
                        z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file));
      if (self->alloc_size < z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file)
          && newdata)
        memset(newdata + self->alloc_size,
               0,
               z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file) - self->alloc_size);
      self->data = newdata;
    }

  self->alloc_size = z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file);

  if (self->size > z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file))
    self->size = z_blob_calculate_allocation_size(req_size, self->alloc_size, self->is_in_file);

  self->stat.alloc_count++;
  self->stat.last_accessed = time(NULL);
  
  z_return();
}

/**
 * Truncates/expands a blob.
 *
 * @param[in] self this
 * @param[in] pos position to truncate at
 * @param[in] timeout timeout
 *
 * @returns TRUE on success
 **/
gboolean
z_blob_truncate(ZBlob *self, gint64 pos, gint timeout)
{
  gboolean      res = FALSE;

  z_enter();
  g_assert(self);
  g_assert(pos >= 0);
  if (z_blob_lock(self, timeout))
    {
      z_blob_alloc(self, pos);
      z_blob_unlock(self);
      res = TRUE;
    }
  z_return(res);
}


/**
 * Write some data into the given position of the blob, expanding it if necessary.
 *
 * @param[in] self this
 * @param[in] pos position to write to
 * @param[in] data data to write
 * @param[in] req_datalen length of data
 * @param[in] timeout timeout
 *
 * @returns The amount of data written.
 **/
gsize
z_blob_add_copy(ZBlob *self, gint64 pos, const gchar* data, gsize req_datalen, gint timeout)
{
  off_t         err;
  gssize        written = 0;

  z_enter();
  g_assert(self);
  g_assert(data);
  g_assert(pos >= 0);
  if (z_blob_lock(self, timeout))
    {
      if (self->alloc_size < (pos + (gssize) req_datalen))
        z_blob_alloc(self, pos + req_datalen);

      if (self->is_in_file)
        {
          gssize remain;
          err = lseek(self->fd, pos, SEEK_SET);
          
          if (err < 0)
            {
              z_log(NULL, CORE_ERROR, 0, "Blob error, lseek() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
              g_assert(0);
            }
          remain = req_datalen;
          while (remain > 0)
            {
              written = write(self->fd, data, remain);
              if (written < 0)
                {
                  if (errno == EINTR)
                    {
                      continue;
                    }
                  else
                    {
                      z_log(NULL, CORE_ERROR, 0, "Blob error, write() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
                      g_assert(0);
                    }
                }
              remain -= written;
            }
        }
      else
        {
          memmove(self->data + pos, data, req_datalen);
          written = req_datalen;
        }
      if (self->size < (pos + written))
        self->size = pos + written;
      self->stat.req_wr++;
      self->stat.total_wr += written;
      self->stat.last_accessed = time(NULL);
      z_blob_unlock(self);
    }
  z_return(written);
}

/**
 * Reads some data from the blob into a buffer.
 *
 * @param[in] self this
 * @param[in] pos position to read from
 * @param[in] data buffer to read into
 * @param[in] req_datalen bytes to read
 * @param[in] timeout timeout
 *
 * @returns The amount of data actually read.
 **/
gsize
z_blob_get_copy(ZBlob *self, gint64 pos, gchar* data, gsize req_datalen, gint timeout)
{
  off_t         err;
  gssize        rd = 0;

  z_enter();
  g_assert(self);
  g_assert(data);
  g_assert(pos >= 0);
  if (pos < self->size)
    {
      if (req_datalen > (guint64) (self->size - pos))
        req_datalen = self->size - pos;
      if (z_blob_lock(self, timeout))
        {
          if (self->is_in_file)
            {
              gssize remain;
              err = lseek(self->fd, pos, SEEK_SET);
              if (err < 0)
                {
                  z_log(NULL, CORE_ERROR, 0, "Blob error, lseek() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
                  g_assert(0);
                }
              remain = req_datalen;
              while (remain > 0)
                {
                  rd = read(self->fd, data, remain);
                  if (rd < 0)
                    {
                      if (errno == EINTR)
                        {
                          continue;
                        }
                      else
                        {
                          z_log(NULL, CORE_ERROR, 0, "Blob error, read() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
                          g_assert(0);
                        }
                    }
                  remain -= rd;
                }
            }
          else
            {
              memmove(data, self->data + pos, req_datalen);
              rd = req_datalen;
            }
          self->stat.req_rd++;
          self->stat.total_rd += rd;
          self->stat.last_accessed = time(NULL);
          z_blob_unlock(self);
        }
    }
  z_return(rd);          
}

/**
 * Get the (absolute) filename assigned to the blob.
 *
 * @param[in] self this
 * @param[in] user Owner of the created file or NULL
 * @param[in] group Group of the created file or NULL
 * @param[in] mode Mode of the created file of -1
 * @param[in] timeout timeout
 *
 * @returns The filename
 **/
const gchar * 
z_blob_get_file(ZBlob *self, const gchar *user, const gchar *group, gint mode, gint timeout)
{
  const gchar   *res = NULL;

  z_enter();
  g_assert(self);
  if (!self->filename || !self->system)
    z_return(NULL);

  if (z_blob_lock(self, timeout))
    {
      if (!self->is_in_file)
        {
          if (self->storage_locked)
            goto exit;

          g_mutex_lock(&self->system->mtx_blobsys); /* swap_out() accesses the blob systems data
                                                      directly, so it needs to be locked */
          z_blob_swap_out(self);
          g_mutex_unlock(&self->system->mtx_blobsys);
        }
      if (group || user)
        {
          uid_t user_id = -1;
          gid_t group_id = -1;
          
          if (user && !z_resolve_user(user, &user_id))
            {
              z_log(NULL, CORE_ERROR, 3, "Cannot resolve user; user='%s'", user);
              goto exit;
            }
          
          if (group && !z_resolve_group(group, &group_id))
            {
              z_log(NULL, CORE_ERROR, 3, "Cannot resolve group; group='%s'", group);
              goto exit;
            }
          
          if (chown(self->filename, user_id, group_id) == -1)
            goto exit;
        }

      if ((mode != -1) && (chmod(self->filename, mode) == -1))
        goto exit;

      res = self->filename;

exit:
      if (res == NULL)
        z_blob_unlock(self);
    }

  z_return(res); 
}

/**
 * Release the lock on the blob after z_blob_get_file.
 *
 * @param[in] self this
 *
 * Besides releasing the lock itself, this function also updates the
 * blob size from the file size.
 **/
void
z_blob_release_file(ZBlob *self)
{
  struct stat st;
  
  z_enter();
  g_assert(self);
  if (!fstat(self->fd, &st))
    self->size = self->alloc_size = st.st_size;
  else
    z_log(NULL, CORE_ERROR, 3, "Cannot stat file on release, blob size may be incorrect from now;");
  z_blob_unlock(self);
  z_return();
}


/**
 * Obtains a pointer to a subrange of the blob.
 *
 * @param[in]      self this
 * @param[in]      pos start of the range to get ptr for
 * @param[in, out] req_datalen length of the range: in=requested, out=mapped
 * @param[in]      timeout timeout
 *
 * This function obtains a pointer to a specified subrange of the blob.
 * Until the pointer is freed by 'z_blob_free_ptr()', the blob will be locked for
 * reading, that means read operations are still possible, but writes and
 * swapping is disabled and will block!
 *
 * @returns The pointer on success, NULL on error
 **/
gchar *
z_blob_get_ptr(ZBlob *self, gint64 pos, gsize *req_datalen, gint timeout)
{
  gchar             *data = NULL;
  gint offset_in_page;

  z_enter();
  g_assert(self);
  g_assert(req_datalen);
  g_assert(self->mapped_ptr == NULL);
  g_assert(pos >= 0);

  if ((pos < self->size) && (self->size > 0) && z_blob_lock(self, timeout))
    {
      if (self->size < (pos + (gssize) *req_datalen))
        *req_datalen = self->size - pos;

      if (self->is_in_file)
        {
          offset_in_page = pos % getpagesize();
          data = (gchar*)mmap(NULL, *req_datalen + offset_in_page, PROT_READ | PROT_WRITE, MAP_SHARED, self->fd, pos - offset_in_page);
          if (data == (gchar*)-1)
            data = NULL;
          else
            data += offset_in_page;
        }
      else
        {
          data = self->data + pos;
        }

      self->mapped_ptr = data;
      self->mapped_length = *req_datalen;

      if (!data)
        z_blob_unlock(self);
    }
  z_return(data);
}

/**
 * Unlocks a blob locked by 'z_blob_get_ptr()'.
 *
 * @param[in] self this
 * @param[in] data Pointer to a range, obtained by 'z_blob_get_ptr()'
 **/
void
z_blob_free_ptr(ZBlob *self, gchar *data)
{
  guint offset_in_page;

  z_enter();
  g_assert(self);
  g_assert(self->mapped_ptr);
  g_assert(self->mapped_ptr == data);
  g_assert(self->mapped_length > 0);
  if (self->is_in_file)
    {
      offset_in_page = GPOINTER_TO_UINT(data) % getpagesize();
      munmap(data - offset_in_page, self->mapped_length + offset_in_page);
    }
  self->mapped_ptr = NULL;
  self->mapped_length = 0;
  z_blob_unlock(self);
  z_return();
}

/**
 * Write data from a buffer into a blob.
 *
 * @param[in]  self this
 * @param[in]  buffer buffer to read from
 * @param[in]  write_pos position to write to
 * @param[in]  timeout timeout
 *
 * Write some data into the given position of the blob, expanding it if
 * necessary. The function takes multiple passes and supports copying gint64
 * chunks and ensures that all the requested data be copied unless an error
 * occurs, thus there is no bytes_read argument.
 *
 * @returns GLib I/O status
 **/

GIOStatus
z_blob_read_from_buffer(ZBlob *self, const std::vector<unsigned char> &buffer, std::size_t write_pos, gint timeout)
{
  g_assert(self);
  g_assert(write_pos >= 0);

  if (!z_blob_lock(self, timeout))
    return G_IO_STATUS_ERROR;

  std::size_t left = buffer.size();
  std::size_t read_pos = 0;
  if (self->is_in_file)
    {
      if (self->size < write_pos)
        z_blob_alloc(self, write_pos);

      off_t err = lseek(self->fd, write_pos, SEEK_SET);
      if (err < 0)
        {
          z_log(NULL, CORE_ERROR, 0, "Blob error, lseek() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
          g_assert(0);
        }

      while (left != 0)
        {
          std::size_t bytes = MIN(left, Z_BLOB_COPY_BUFSIZE);

          if (self->alloc_size < (write_pos + bytes))
            z_blob_alloc(self, write_pos + bytes);

          left -= bytes;
          write_pos += bytes;
          if (self->size < write_pos)
            self->size = write_pos;

          std::size_t remain = bytes;
          while (remain > 0)
            {
              std::size_t bw = write(self->fd, buffer.data() + read_pos, remain);
              if (bw < 0)
                {
                  if (errno == EINTR)
                    {
                      continue;
                    }
                  else
                    {
                      z_log(NULL, CORE_ERROR, 0, "Blob error, write() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
                      g_assert(0);
                    }
                }
              remain -= bw;
              read_pos += bw;
            }
        }
    }
  else
    {
      while (left != 0)
        {
          std::size_t bytes = MIN(left, Z_BLOB_COPY_BUFSIZE);
          if (self->alloc_size < (write_pos + bytes))
            z_blob_alloc(self, write_pos + buffer.size());

          std::copy(buffer.data() + read_pos, buffer.data() + read_pos + bytes, self->data + write_pos);
          left -= bytes;
          write_pos += bytes;
          read_pos += bytes;
          if (self->size < write_pos)
            self->size = write_pos;
        }
    }

  self->stat.req_wr++;
  self->stat.total_wr += buffer.size();
  self->stat.last_accessed = time(NULL);

  z_blob_unlock(self);

  return G_IO_STATUS_NORMAL;
}

/**
 * Write data read from a stream into a blob.
 *
 * @param[in]  self this
 * @param[in]  pos position to write to
 * @param[in]  stream stream to read from
 * @param[in]  count length of data to read
 * @param[in]  timeout timeout
 * @param[out] error stream error if return value is G_IO_STATUS_ERROR
 *
 * Write some data into the given position of the blob, expanding it if
 * necessary. The function takes multiple passes and supports copying gint64
 * chunks and ensures that all the requested data be copied unless an error
 * occurs, thus there is no bytes_read argument.
 *
 * @returns GLib I/O status
 **/
GIOStatus
z_blob_read_from_stream(ZBlob *self, gint64 pos, ZStream *stream, gint64 count, gint timeout, GError **error)
{
  off_t err;
  GIOStatus res = G_IO_STATUS_NORMAL;
  guchar *copybuf;
  GError *local_error = NULL;
  gsize left;

  z_enter();
  g_assert(self);
  g_assert(pos >= 0);
  g_return_val_if_fail((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);

  if (z_blob_lock(self, timeout))
    {
      if (self->is_in_file)
        {
          if (self->size < pos)
            z_blob_alloc(self, pos);

          err = lseek(self->fd, pos, SEEK_SET);
          if (err < 0)
            {
              z_log(NULL, CORE_ERROR, 0, "Blob error, lseek() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
              g_assert(0);
            }

          copybuf = g_new(guchar, Z_BLOB_COPY_BUFSIZE);
          left = count;
          while (left != 0)
            {
              gsize br;
              gssize bw;
              gsize bytes;
              gssize remain;

              bytes = MIN(left, Z_BLOB_COPY_BUFSIZE);

              if (self->alloc_size < (pos + (gssize) bytes))
                z_blob_alloc(self, pos + bytes);

              res = z_stream_read(stream, copybuf, bytes, &br, &local_error);
              if (res != G_IO_STATUS_NORMAL)
                goto exit_stats;

              left -= br;
              pos += br;
              if (self->size < pos)
                self->size = pos;

              remain = br;
              while (remain > 0)
                {
                  bw = write(self->fd, copybuf, remain);
                  if (bw < 0)
                    {
                      if (errno == EINTR)
                        {
                          continue;
                        }
                      else
                        {
                          z_log(NULL, CORE_ERROR, 0, "Blob error, write() failed; file='%s', error='%s'", self->filename, g_strerror(errno));
                          g_assert(0);
                        }
                    }
                  remain -= bw;
                }
            }
          g_free(copybuf);
        }
      else
        {
          left = count;
          while (left != 0)
            {
              gsize br;
              gsize bytes;
              
              bytes = MIN(left, Z_BLOB_COPY_BUFSIZE);
              if (self->alloc_size < (pos + (gssize) bytes))
                z_blob_alloc(self, pos + count);
              
              res = z_stream_read(stream, self->data + pos, bytes, &br, &local_error);
              if (res != G_IO_STATUS_NORMAL)
                goto exit_stats;
              left -= br;
              pos += br;
              if (self->size < pos)
                self->size = pos;
            }
        }
        
    exit_stats:
    
      self->stat.req_wr++;
      self->stat.total_wr += count;
      self->stat.last_accessed = time(NULL);

      z_blob_unlock(self);
    }
  else
    {
      /* FIXME: how to map this error ? */
      res = G_IO_STATUS_ERROR;
      g_set_error(error, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Error acquiring blob lock");
    }
  if (local_error)
    g_propagate_error(error, local_error);
  z_return(res);
}

GIOStatus z_blob_write_to_buffer(ZBlob *self, std::vector<unsigned char> &buffer, std::size_t read_count, std::size_t read_pos, gint timeout)
{
  g_assert(self);
  g_assert(read_pos >= 0);

  std::size_t end_pos = read_pos + read_count;
  std::size_t write_pos = 0;
  if (buffer.capacity() < read_count)
    buffer.resize(read_count);
  while (read_pos < end_pos)
    {
      gsize mapped_pos, bw;
      gchar *d;

      std::size_t mapped_length = MIN(Z_BLOB_COPY_BUFSIZE, end_pos - read_pos);
      d = z_blob_get_ptr(self, read_pos, &mapped_length, timeout);
      if (!d)
        {
          return G_IO_STATUS_ERROR;
        }
      std::copy(d, d + mapped_length, buffer.data() + write_pos);
      z_blob_free_ptr(self, d);
      read_pos += mapped_length;
      write_pos += mapped_length;
    }

  return G_IO_STATUS_NORMAL;
}

/**
 * Write data from a blob to a stream.
 *
 * @param[in]  self this
 * @param[in]  pos position to write to
 * @param[in]  stream stream to read from
 * @param[in]  count length of data to read
 * @param[in]  timeout timeout
 * @param[out] error stream error if one occurs
 *
 * Write some data from the given position of the blob to the stream. The
 * function takes multiple passes thus it supports copying gint64 sized
 * chunks. It also ensures that the complete requested chunk is written
 * unless an error occurs, thus there is no bytes_written argument.
 *
 * @returns GLib I/O status
 **/
GIOStatus
z_blob_write_to_stream(ZBlob *self, gint64 pos, ZStream *stream, gint64 count, gint timeout, GError **error)
{
  gint64 end_pos = pos + count;
  GIOStatus res = G_IO_STATUS_NORMAL;

  g_assert(self);
  g_assert(pos >= 0);
  g_return_val_if_fail((error == NULL) || (*error == NULL), G_IO_STATUS_ERROR);
  
  while (pos < end_pos)
    {
      gsize mapped_length, mapped_pos, bw;
      gchar *d;

      mapped_length = MIN(Z_BLOB_COPY_BUFSIZE, end_pos - pos);
      mapped_pos = 0;
      d = z_blob_get_ptr(self, pos, &mapped_length, timeout);
      if (!d)
        {
          res = G_IO_STATUS_ERROR;
          break;
        }
      if (z_stream_write_chunk(stream, d + mapped_pos, mapped_length, &bw, NULL) != G_IO_STATUS_NORMAL)
        {
          res = G_IO_STATUS_ERROR;
          z_blob_free_ptr(self, d);
          goto exit;
        }
      z_blob_free_ptr(self, d);
      pos += mapped_length;
    }
 exit:
  return res;
}

/**
 * Sets the blob's storage_locked field to st.
 *
 * @param[in] self this
 * @param[in] st the value to set storage_locked to.
 *
 * @todo FIXME-DOC: this might need better documentation.
 **/
void
z_blob_storage_lock(ZBlob *self, gboolean st)
{
  self->storage_locked = st;
}

/**
 * @file
 *
 * @todo optimisation of pre-allocation
 *
 * Reason: simply doubling of pre-allocated space wastes the address range:
 *
 * <pre>
 * |- x -|
 *       |--- 2x ---|
 *                  |-------- 4x --------|
 * </pre>
 *
 * etc. There's no way to use the deallocated address space, because, for
 * example the next allocation of 8x can't fit in the x+2x = 3x hole.
 * 
 * To achieve this, a growth factor less than 2 must be used:
 *
 * <pre>
 * |- x -|
 *       |- (p^1)*x -|
 *                   |---- (p^2)*x ----|
 * |---- (p^3)*x ----|
 * </pre>
 * 
 * The exact value of p is the solution of the equation
 *
 * <pre>
 *             p^3 = p + 1
 *     p^3 - p - 1 = 0
 * </pre>
 *
 * which is (cbrt(x) ::= the cubic root of x)
 *
 * <pre>
 *    p <= cbrt( (1+sqrt(23/27))/2 ) + cbrt( (1-sqrt(23/27))/2 ) 
 *    p =~= 1.3247179572447458
 * </pre>
 * 
 * This can be approximated quite well by
 * <pre>
 *     p = 1 + 1/3 - 1/116 
 * </pre>
 * and its reciprocal can be approximated by
 * <pre>
 *   1/p = 1 - 1/4 - 1/205
 * </pre>
 * 
 * Unfortunately, using integers the rounding error accumulates, and this has
 * two bad side-effects:
 *  - sometimes the calculated next allocation size is off the range by one
 *  - growing and shrinking back is also off by one in some cases
 *  
 * Because this optimisation affects only allocations comparable to the size of
 * the address space (>= 4 GB), currently I use simple doubling, but this must
 * be fixed some time.
 **/
