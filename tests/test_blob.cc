#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include <zorpll/blob.h>
#include <zorpll/thread.h>
#include <zorpll/log.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <stdarg.h>
#include <stdlib.h>

#define TEST_DELAY    100

/***********************************************************************
 * Concurrent access test
 *
 ***********************************************************************/


void
send_log(void *p G_GNUC_UNUSED, const gchar *s G_GNUC_UNUSED, gint n G_GNUC_UNUSED, const char *fmt, ...)
{
  va_list vl;

  va_start(vl, fmt);
  vprintf(fmt, vl);
  printf("\n"); fflush(stdout);
  va_end(vl);
}

#define b2str(a) a ? "TRUE" : "FALSE"

void
test_and_log(gboolean condition, gboolean expected, gchar *log_format, ...)
{
  va_list vl;
  gchar orig_log[4096];
  
  va_start(vl, log_format);
  g_vsnprintf(orig_log, sizeof(orig_log), log_format, vl);
  va_end(vl);
  
  printf("%s, expected='%s', condition='%s'\n", orig_log, b2str(expected), b2str(condition));
  
  BOOST_CHECK(condition == expected);
}

/***********************************************************************
 * Growing allocation sizes test
 *
 ***********************************************************************/

/**
 * test_growing_sizes:
 * @blobsys: this
 *
 * Growing allocation sizes test main function
 */
void
test_growing_sizes(ZBlobSystem *blobsys)
{
  ZBlob       *blob;
  int     i;
  char    p;
  
  /* create a blob */
  blob = z_blob_new(blobsys, 100); /* z_blob_new(NULL, ...) to use the default blobsys */
  send_log(NULL, CORE_DEBUG, 4, "-- created blob; blob='%p'", blob);
 
  p = '\xd0'; /* why not */
  for (i=1; i<0x10000; i++)
    {
      if (!(i & 0x3ff))
        send_log(NULL, CORE_DEBUG, 4, "-- writing; blob='%p', pos='%d'", blob, i);
      z_blob_add_copy(blob, i, &p, 1, -1);
      usleep(TEST_DELAY);
    }
}


/***********************************************************************
 * Blob fetching-in test
 *
 ***********************************************************************/

/**
 * test_fetch_in:
 * @blobsys: this
 *
 * Fetch-in test main function
 */
void
test_fetch_in(ZBlobSystem *blobsys)
{
  ZBlob     *blob[3];

  send_log(NULL, CORE_DEBUG, 4, "-- creating blob[0]; size='2000'");
  blob[0] = z_blob_new(blobsys, 2000);  /* will be allocated in mem */
  send_log(NULL, CORE_DEBUG, 4, "-- creating blob[1]; size='2000'");
  blob[1] = z_blob_new(blobsys, 2000);  /* ditto */
  send_log(NULL, CORE_DEBUG, 4, "-- creating blob[2]; size='2000'");
  blob[2] = z_blob_new(blobsys, 2000);  /* out of limit -> goes to disk */
  send_log(NULL, CORE_DEBUG, 4, "-- destroying blob[0]");
  z_blob_unref(blob[0]);              /* b0 dies, still above lowat, nothing happens */
  send_log(NULL, CORE_DEBUG, 4, "-- destroying blob[1]");
  z_blob_unref(blob[1]);              /* b1 dies, b2 must be fetched in */
  send_log(NULL, CORE_DEBUG, 4, "-- sleeping 2 seconds to leave time for fetch in... (shall be fetched in)");
  sleep(2);
  test_and_log(blob[2]->is_in_file, FALSE, "-- blob[2]->is_in_file: %s", blob[2]->is_in_file ? "TRUE" : "FALSE");
  send_log(NULL, CORE_DEBUG, 4, "-- exiting and destroying blob[2]");
  z_blob_unref(blob[2]); 
}


/***********************************************************************
 * Deferred allocation test
 *
 ***********************************************************************/

gpointer
mk_blob_deferred1(ZBlobSystem *blobsys)
{
  ZBlob     *blob;
  
  send_log(NULL, CORE_DEBUG, 4, "---- mk_blob_deferred1, creating blob; size='4000'");
  blob = z_blob_new(blobsys, 4000);  /* can't be allocated until another blob is destroyed */
  send_log(NULL, CORE_DEBUG, 4, "---- mk_blob_deferred1, blob created, sleeping 10 sec;");
  sleep(10);
  send_log(NULL, CORE_DEBUG, 4, "---- mk_blob_deferred1, destroying blob;");
  z_blob_unref(blob);
  send_log(NULL, CORE_DEBUG, 4, "---- mk_blob_deferred1, exiting;");
  return NULL;
}

gpointer
mk_blob_deferred2(ZBlobSystem *blobsys)
{
  ZBlob     *blob;
  
  send_log(NULL, CORE_DEBUG, 4, "---- mk_blob_deferred2, creating blob; size='4000'");
  blob = z_blob_new(blobsys, 4000);  /* can't be allocated until another blob is destroyed */
  send_log(NULL, CORE_DEBUG, 4, "---- mk_blob_deferred2, blob created, sleeping 10 sec;");
  sleep(10);
  send_log(NULL, CORE_DEBUG, 4, "---- mk_blob_deferred2, destroying blob;");
  z_blob_unref(blob);
  send_log(NULL, CORE_DEBUG, 4, "---- mk_blob_deferred2, exiting;");
  return NULL;
}

/**
 * test_deferred_alloc:
 * @blobsys: this
 *
 * Deferred allocation test main function
 */
void
test_deferred_alloc(ZBlobSystem *blobsys)
{
  ZBlob         *blob[2];
  GThread       *thr[2];
  
  send_log(NULL, CORE_DEBUG, 4, "-- creating blob[0]; size='4500'");
  blob[0] = z_blob_new(blobsys, 4500);  /* will be allocated in mem - 500 bytes remaining*/
  send_log(NULL, CORE_DEBUG, 4, "-- creating blob[1]; size='9500'");
  blob[1] = z_blob_new(blobsys, 9500);  /* will be allocated on disk - 500 bytes remaining */

  send_log(NULL, CORE_DEBUG, 4, "-- creating threads for another 2 allocations");
  thr[0] = g_thread_create((GThreadFunc)mk_blob_deferred1, (gpointer)blobsys, TRUE, NULL);
  thr[1] = g_thread_create((GThreadFunc)mk_blob_deferred2, (gpointer)blobsys, TRUE, NULL);
  send_log(NULL, CORE_DEBUG, 4, "-- thread created, sleeping 3 sec");
  sleep(3);
  send_log(NULL, CORE_DEBUG, 4, "-- destroying blob[0]"); /* the deferred alloc shall succeed now */
  z_blob_unref(blob[0]); 
  send_log(NULL, CORE_DEBUG, 4, "-- sleeping 3 sec");
  sleep(3);
  send_log(NULL, CORE_DEBUG, 4, "-- destroying blob[1]"); /* the deferred alloc shall succeed now */
  z_blob_unref(blob[1]); 
  
  send_log(NULL, CORE_DEBUG, 4, "-- waiting for the threads");
  g_thread_join(thr[0]);
  g_thread_join(thr[1]);

  send_log(NULL, CORE_DEBUG, 4, "-- threads finished, exiting");
}

/**
 * test_fetch_in_lock:
 * @blobsys: this
 *
 * Fetch-in locking test main function
 */
void
test_fetch_in_lock(ZBlobSystem *blobsys)
{
  ZBlob     *blob[3];

  send_log(NULL, CORE_DEBUG, 4, "-- creating blob[0]; size='2000'");
  blob[0] = z_blob_new(blobsys, 2000);  /* will be allocated in mem */
  send_log(NULL, CORE_DEBUG, 4, "-- creating blob[1]; size='2000'");
  blob[1] = z_blob_new(blobsys, 2000);  /* ditto */
  send_log(NULL, CORE_DEBUG, 4, "-- creating blob[2]; size='2000'");
  blob[2] = z_blob_new(blobsys, 2000);  /* out of limit -> goes to disk */
  z_blob_get_file(blob[2], NULL, NULL, 664, 0);
  z_blob_storage_lock(blob[2], TRUE);       /* locking b2 - fetch-in disabled */
  z_blob_release_file(blob[2]);
  send_log(NULL, CORE_DEBUG, 4, "-- destroying blob[0]");
  z_blob_unref(blob[0]);              /* b0 dies, still above lowat, nothing happens */
  send_log(NULL, CORE_DEBUG, 4, "-- destroying blob[1]");
  z_blob_unref(blob[1]);              /* b1 dies, but b2 can't be fetched in */
  send_log(NULL, CORE_DEBUG, 4, "-- sleeping 2 seconds to leave time for fetch in... (shall remain in file)");
  sleep(2);
  test_and_log(blob[2]->is_in_file, TRUE, "-- blob[2]->is_in_file: %s", blob[2]->is_in_file ? "yes" : "no");
  send_log(NULL, CORE_DEBUG, 4, "-- exiting and destroying blob[2]");
  z_blob_unref(blob[2]); 
}

/**
 * test_swap_out_and_then_released:
 * @blobsys: this
 *
 * This function test the scenario when the blob swapped out and then
 * scanned, and after that released.
 * The size of the blob should not change after this scenario.
 */
void
test_swap_out_and_then_released(ZBlobSystem *blobsys)
{
  ZBlob *blob;
  char p;
  int i;

  blob = z_blob_new(blobsys, 0);  /* will be allocated in mem - 500 bytes remaining*/

  p = '\xd0'; /* why not */
  for (i = 0; i < 5500; i++)
    {
      z_blob_add_copy(blob, i, &p, 1, -1);
    }
  test_and_log(blob->size==5500, TRUE, "-- blob->size (before): %ld", blob->size);
  test_and_log(blob->is_in_file, TRUE, "-- blob[2]->is_in_file: %s", blob->is_in_file ? "yes" : "no");
  z_blob_lock(blob, -1);
  z_blob_release_file(blob);
  test_and_log(blob->size==5500, TRUE, "-- blob->size (after): %ld", blob->size);
}


/***********************************************************************
 * 'Framework'
 *
 ***********************************************************************/

BOOST_AUTO_TEST_CASE(test_blob)
{
  ZBlobSystem *blobsys;
  ZBlob *blob;
  /*gchar *blobptr;*/
  /*gsize blobptr_size;*/
  /*const gchar *blobfile;*/

  z_thread_init();
  /*verbose_level=9;*/
  send_log(NULL, CORE_DEBUG, 4, "============= STARTING TEST ================");

  /* Initialise default blob system */
  z_blob_system_default_init();

  /* Initialise custom blob system */
  blobsys = z_blob_system_new("/tmp", 10000, 5000, 1000, 2000, 500);

  /*test_growing_sizes(blobsys);*/
  test_fetch_in(blobsys);
  test_fetch_in_lock(blobsys);
  test_deferred_alloc(blobsys);
  test_swap_out_and_then_released(blobsys);
 
  /* Deinitialie custom blob system */
  z_blob_system_unref(blobsys);

  /* Create blob in the default blob system */
  blob = z_blob_new(NULL, 500);
  if (blob == NULL)
    {
      send_log(NULL, CORE_DEBUG, 3, "Cannot alocate blob in the default blob system;");
    }
  else
    {
      /*blobptr_size = 10;*/
      /*blobptr = z_blob_get_ptr(blob, 0, &blobptr_size, -1);*/
      /*blobfile =*/ z_blob_get_file(blob, NULL, NULL, 0644, -1);
      /* Leave it allocated and locked, let's see what happens when the blobsys is destroyed */
    }
  
  /* Deinitialise default blob system */
  z_blob_system_default_destroy();
  z_thread_destroy();
  send_log(NULL, CORE_DEBUG, 4, "============= TEST FINISHED ================");
}
