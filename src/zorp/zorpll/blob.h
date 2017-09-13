/***************************************************************************
 *
 * This file is covered by a dual licence. You can choose whether you
 * want to use it according to the terms of the GNU GPL version 2, or
 * under the terms of Zorp Professional Firewall System EULA located
 * on the Zorp installation CD.
 *
 ***************************************************************************/

#ifndef ZORP_BLOB_H_INCLUDED
#define ZORP_BLOB_H_INCLUDED

#include <zorpll/zorplib.h>
#include <zorpll/stream.h>
#include <time.h>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

struct ZBlob;

/**
 * Central management of blobs.
 **/
typedef struct ZBlobSystem
{
  ZRefCount     ref_cnt;                    /**< reference counter */
  gchar         *dir;                       /**< directory to store the blobs in */
  guint64       disk_max, disk_used;        /**< maximal and current disk usage */
  gsize         mem_max, mem_used;          /**< maximal and current memory usage */
  gsize         lowat, hiwat, noswap_max;   /**< control limits - see spec */
  
  GMutex        mtx_blobsys;               /**< gadgets used for signalling request like allocation, etc. */
  GCond         cond_thread_started;
  
  GThread       *thr_management;            /**< management thread */
  GError        *thread_error;              /**< error structure for creating thr_management */
  GList         *blobs;                     /**< blobs created in this blob system */

  GAsyncQueue   *req_queue;                 /**< queue of blobs who have pending requests */
  GList         *waiting_list;              /**< list of blobs whose requests weren't approved immediately */
  gboolean      active;                     /**< false if the blobsys is 'under destruction' */
} ZBlobSystem;

/** global default instance */
extern ZBlobSystem  *z_blob_system_default;
extern const gchar* z_blob_system_default_tmpdir;   /**< directory to store the blobs in */
extern gint64 z_blob_system_default_max_disk_usage; /**< max disk usage = 1 GB */
extern gsize z_blob_system_default_max_mem_usage;   /**< max mem usage = 256 MB */
extern gsize z_blob_system_default_lowat;           /**< lowat = 96 MB */
extern gsize z_blob_system_default_hiwat;           /**< hiwat = 128 MB */
extern gsize z_blob_system_default_noswap_max;      /**< noswap_max = 16 kB */

/* constructor, ref, unref, destructor */
ZBlobSystem* z_blob_system_new(const char *dir, gint64 dmax, gsize mmax, gsize low, gsize hiw, gsize nosw);
void z_blob_system_ref(ZBlobSystem *self);
void z_blob_system_unref(ZBlobSystem *self);
/* create and destroy the default instance */
void z_blob_system_default_init(void);
void z_blob_system_default_destroy(void);


/**
 * Usage statistics for a blob.
 **/
typedef struct ZBlobStatistic
{
    gint                req_rd, req_wr, req_map;  /**< performed read, write and mapping requests */
    gint                swap_count;               /**< swapout counter */
    gint                alloc_count;              /**< alloc modification counter */
    unsigned long long  total_rd, total_wr;       /**< total bytes read and written */
    time_t              created, last_accessed;   /**< time of creation and last access */
} ZBlobStatistic;

/* initialise a blob stat - counters set to zero, timestamps to 'now' */
void z_blob_statistic_init(ZBlobStatistic *self);

/** Blob sends ZBlobRequestCode to the maintenance thread for approval or as notification. */ 
typedef enum ZBlobRequestCode
{
  Z_BLOB_REQ_NONE,                          /**< none - default value, does nothing */
  Z_BLOB_REQ_REGISTER,                      /**< new blob wants to register itself */
  Z_BLOB_REQ_UNREGISTER,                    /**< blob wants to unregister itself */
  Z_BLOB_REQ_ALLOC                          /**< blob asks for approval on modification of its allocation */
} ZBlobRequestCode;

/** The blob itself. */
typedef struct ZBlob
{
  ZRefCount         ref_cnt;                /**< reference counter */
  gint64            size, alloc_size;       /**< actual size of the blob and the allocated space */
  gboolean          is_in_file;             /**< is the blob swapped out */
  gchar             *filename;              /**< swapfile name */
  gint              fd;                     /**< swapfile descriptor */
  gchar             *data;                  /**< memory image pointer */
  ZBlobSystem       *system;                /**< blob system it belongs to */
  GMutex            mtx_lock;              /**< lock for concurrent accesses */
  ZBlobStatistic    stat;                   /**< statistics */

  GMutex            mtx_reply;             /**< mutex and conditional for waiting for reply */
  GCond             cond_reply;
  gboolean          replied;
  
  gchar             *mapped_ptr;            /**< addr and length of the mapped area */
  gsize             mapped_length;          /**< (when multiple mappings will be implemented, replace with ?GHash?) */

  /* communication with the blobsystems threadproc */
  gssize            alloc_req;              /**< communication with the blobsystems threadproc */
  gboolean          approved;               /**< communication with the blobsystems threadproc */
  gboolean          storage_locked;         /**< communication with the blobsystems threadproc */
} ZBlob;

/* constructor, ref, unref, destructor */
ZBlob *z_blob_new(ZBlobSystem *sys, gsize initial_size);
ZBlob *z_blob_ref(ZBlob *self);
void z_blob_unref(ZBlob *self);

/* swap out and get the filename - WARNING - the blob will be locked for reading (only) */
const gchar *z_blob_get_file(ZBlob *self, const gchar *user, const gchar *group, gint mode, gint timeout);
void z_blob_release_file(ZBlob *self);

/* truncate/expand a blob to a specified size */
gboolean z_blob_truncate(ZBlob *self, gint64 pos, gint timeout);

/* write and read */
gsize z_blob_add_copy(ZBlob *self, gint64 pos, const gchar *data, gsize req_datalen, gint timeout);
gsize z_blob_get_copy(ZBlob *self, gint64 pos, gchar *data, gsize req_datalen, gint timeout);

GIOStatus z_blob_read_from_stream(ZBlob *self, gint64 pos, ZStream *stream, gint64 count, gint timeout, GError **error);
GIOStatus z_blob_write_to_stream(ZBlob *self, gint64 pos, ZStream *stream, gint64 count, gint timeout, GError **error);

/* lock and unlock part of the blob and return pointer to it - WARNING - the blob will be locked for reading (only) */
gchar *z_blob_get_ptr(ZBlob *self, gint64 pos, gsize *req_datalen, gint timeout);
void z_blob_free_ptr(ZBlob *self, gchar *data);

void z_blob_storage_lock(ZBlob *self, gboolean st);

/* locking functions - needed by ZStreamBlob */
gboolean z_blob_lock(ZBlob *self, gint timeout);
void z_blob_unlock(ZBlob *self);

#ifdef __cplusplus
}
#endif

GIOStatus z_blob_read_from_buffer(ZBlob *self, const std::vector<unsigned char> &buffer, std::size_t write_pos = 0, gint timeout = -1);

GIOStatus z_blob_write_to_buffer(ZBlob *self, std::vector<unsigned char> &buffer, std::size_t read_count, std::size_t read_pos = 0, gint timeout = -1);

#endif

