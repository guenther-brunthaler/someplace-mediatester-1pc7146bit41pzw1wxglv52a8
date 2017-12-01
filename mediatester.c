/* Media tester
 *
 * Fills a block device or data stream with reprocucible pseudorandom bytes or
 * reads such a block device or data stream to verify the same pseudorandom
 * bytes written before are still present. Both write and verification mode
 * try to make use of the available CPU cores to create the pseudorandom data
 * data as quickly as possible in parallel, and double buffering is employed
 * to allow disk I/O to run (mostly) in parallel, too.
 *
 * Copyright (c) 2017 Guenther Brunthaler. All rights reserved.
 *
 * This source file is free software.
 * Distribution is permitted under the terms of the GPLv3.  */

#include <dim_sdbrke8ae851uitgzm4nv3ea2.h>
#include <pearson.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>

/* TWO such buffers will be allocated. */
#define APPROXIMATE_BUFFER_SIZE (16ul << 20)

static struct {
   int write_mode;
   uint8_t *shared_buffer, *shared_buffers[2];
   uint8_t const *shared_buffer_stop;
   size_t blksz;
   size_t work_segments;
   size_t work_segment_sz;
   uint_fast64_t pos;
   unsigned active_threads /* = 0; */;
   pthread_mutex_t workers_mutex;
   pthread_mutex_t io_mutex;
   pthread_cond_t workers_wakeup_call;
} tgs; /* Thread global storage */

#define CEIL_DIV(num, den) (((num) + (den) - 1) / (den))

#define ERROR(msg) { error= msg; goto fail; }

static char const exotic_error_msg[]= {
   "Internal error! (This should normally never happen.)"
};

static char const write_error_msg[]= {"Write error!"};

/* We *could* pass &tgs as a paramater, but there is no point because there
 * exists only one such instance anyway. Also, accessing global variables is
 * typically faster. */
static void *thread_func(void *unused_dummy) {
   char const *error= 0;
   struct { unsigned workers_mutex_procured; } have= {0};
   (void)unused_dummy;
   if (pthread_mutex_lock(&tgs.workers_mutex)) {
      lock_error:
      ERROR("Could not lock mutex!");
   }
   have.workers_mutex_procured= 1;
   check_for_work:
   if (tgs.shared_buffer == tgs.shared_buffer_stop) {
      /* All worker segments have already been assigned to some thread. */
      if (tgs.active_threads == 0) {
         /* And all the worker threads have finished! Let's do I/O then. */
         for (;;) {
            ssize_t written;
            uint8_t const *out;
            size_t left;
            out= tgs.shared_buffer;
            if ((written= write(1, out, left)) <= 0) {
               if (written == 0) break;
               if (written != -1) unlikely_error: ERROR(exotic_error_msg);
               if (errno != EINTR) ERROR(write_error_msg);
               written= 0;
            }
            if ((size_t)written > left) goto unlikely_error;
            out+= (size_t)written;
            left-= (size_t)written;
         }
         if (pthread_cond_broadcast(&tgs.workers_wakeup_call)) {
            ERROR("Could not wake up worker threads!");
         }
      } else {
         /* We have nothing to do, but other worker threads are still active.
          * Just wait until there is again possibly something to do. */
         --tgs.active_threads;
         if (pthread_cond_wait(&tgs.workers_wakeup_call, &tgs.workers_mutex)) {
            ERROR("Could not wait for condition variable!");
         }
      }
   } else {
      /* There is more work to do. Seize the next work segment. */
      pearnd_offset po;
      uint8_t *work_segment= tgs.shared_buffer;
      tgs.shared_buffer+= tgs.work_segment_sz;
      pearnd_seek(&po, tgs.pos);
      tgs.pos+= tgs.work_segment_sz;
      /* Allow other threads to seize work segments as well. */
      have.workers_mutex_procured= 0;
      if (pthread_mutex_unlock(&tgs.workers_mutex)) goto unlock_error;
      /* Do every worker thread's primary job: Process its work segment. */
      pearnd_generate(work_segment, tgs.work_segment_sz, &po);
      /* See whether we can get the next job. */
      if (pthread_mutex_lock(&tgs.workers_mutex)) goto lock_error;
      goto check_for_work;
   }
   fail:
   if (have.workers_mutex_procured) {
      have.workers_mutex_procured= 0;
      if (pthread_mutex_unlock(&tgs.workers_mutex)) {
         unlock_error:
         ERROR("Could not unlock mutex!");
      }
   }
   return (void *)error;
}
              
static uint_fast64_t atou64(char const **error, char const *numeric) {
   uint_fast64_t v= 0, nv;
   unsigned digit, i;
   for (i= 0; ; ++i) {
      #define error *error
         switch (numeric[i]) {
            default: ERROR("Invalid decimal digit!");
            case '\0':
               if (i) return v;
               ERROR("Decimal number without any digits!");
            case '0': digit= 0; break;
            case '1': digit= 1; break;
            case '2': digit= 2; break;
            case '3': digit= 3; break;
            case '4': digit= 4; break;
            case '5': digit= 5; break;
            case '6': digit= 6; break;
            case '7': digit= 7; break;
            case '8': digit= 8; break;
            case '9': digit= 9;
         }
         if ((nv= v * 10 + digit) < v) ERROR("Decimal number is too large!");
      #undef error
      v= nv;
   }
   fail:
   return 0;
}

int main(int argc, char **argv) {
   char const *error= 0;
   pearnd_offset po;
   size_t shared_buffer_size;
   unsigned threads;
   pthread_t *tid;
   char *tvalid;
   struct {
      unsigned tvalid: 1;
      unsigned tid: 1;
      unsigned workers_mutex: 1;
      unsigned io_mutex: 1;
      unsigned workers_wakeup_call: 1;
   } have= {0};
   /* Preset global variables for interthread communication. */
   tgs.blksz= 4096;
   tgs.work_segments= 64;
   if (pthread_mutex_init(&tgs.workers_mutex, 0)) goto unlikely_error;
   have.workers_mutex= 1;
   if (pthread_mutex_init(&tgs.io_mutex, 0)) goto unlikely_error;
   have.io_mutex= 1;
   if (pthread_cond_init(&tgs.workers_wakeup_call, 0)) goto unlikely_error;
   have.workers_wakeup_call= 1;
   /* Process arguments. */
   if (argc < 3 || argc > 4) {
      bad_arguments:
      ERROR(
         "Arguments: (write | verify) <password> [ <starting_byte_offset> ]"
      );
   }
   if (!strcmp(argv[1], "write")) tgs.write_mode= 1;
   else if (!strcmp(argv[1], "verify")) tgs.write_mode= 0;
   else goto bad_arguments;
   if (!*argv[2]) ERROR("Key must not be empty!");
   pearnd_init(argv[2], strlen(argv[2]));
   if (argc == 4) {
      tgs.pos= atou64(&error, argv[3]); if (error) goto fail;
      if (tgs.pos % tgs.blksz) {
         ERROR("Starting offset must be a multiple of the I/O block size!");
      }
      if ((off_t)tgs.pos < 0) ERROR("Numeric overflow in offset!");
      if (
         lseek(tgs.write_mode ? 1 : 0, (off_t)tgs.pos, SEEK_SET) == (off_t)-1
      ) {
         ERROR("Could not reposition standard stream to starting position!");
      }
   } else {
      tgs.pos= 0;
   }
   {
      long rc;
      if (
            (rc= sysconf(_SC_NPROCESSORS_ONLN)) == -1
         || (threads= (unsigned)rc , (long)threads != rc)
      ) {
         ERROR("Could not determine number of available CPU processors!");
      }
   }
   if (threads < tgs.work_segments) {
      if (threads == 1) tgs.work_segments= 1;
      tgs.work_segments= tgs.work_segments / threads * threads;
      assert(tgs.work_segments >= 1);
   } else {
      tgs.work_segments= threads;
   }
   /* Most threads will generate PRNG data. Another one does I/O and switches
    * working buffers when the next buffer is ready. The main program thread
    * only waits for termination of the other threads. */
   ++threads;
   tgs.work_segment_sz= CEIL_DIV(APPROXIMATE_BUFFER_SIZE, tgs.work_segments);
   tgs.work_segment_sz= CEIL_DIV(tgs.work_segment_sz, tgs.blksz) * tgs.blksz;
   shared_buffer_size= tgs.work_segment_sz * tgs.work_segments;
   if (
      fprintf(
            stderr
         ,  "Starting output offset: %" PRIdFAST64 " bytes\n"
            "Optimum device I/O block size: %u\n"
            "PRNG worker threads: %u\n"
            "worker's buffer segment size: %lu bytes\n"
            "number of worker segments: %lu\n"
            "size of buffer subdivided into worker segments: %lu bytes\n"
            "number of such buffers: %u\n"
         ,  tgs.pos
         ,  (unsigned)tgs.blksz
         ,  threads - 1
         ,  (unsigned long)tgs.work_segment_sz
         ,  (unsigned long)tgs.work_segments
         ,  (unsigned long)shared_buffer_size
         ,  (unsigned)DIM(tgs.shared_buffers)
      ) <= 0
   ) {
      goto write_error;
   }
   {
      unsigned i;
      for (i= (unsigned)DIM(tgs.shared_buffers); i--; ) {
         if (
            (
               tgs.shared_buffers[i]= mmap(
                     0, shared_buffer_size, PROT_READ | PROT_WRITE
                  ,  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
               )
            ) == MAP_FAILED
         ) {
            ERROR("Could not allocate I/O buffer!");
         }
      }
   }
   tgs.shared_buffer_stop=
      tgs.shared_buffer= tgs.shared_buffers[0] + shared_buffer_size
   ;
   pearnd_seek(&po, tgs.pos);
   if (!tgs.write_mode) ERROR("Verify mode is not yet implemented!");
   if (!(tid= calloc(threads, sizeof *tid))) {
      malloc_error:
      ERROR("Memory allocation failure!");
   }
   have.tid= 1;
   if (!(tvalid= calloc(threads, sizeof *tvalid))) goto malloc_error;
   have.tvalid= 1;
   {
      unsigned i;
      for (i= threads; i--; ) {
         if (pthread_create(&tid[i], 0, &thread_func, 0)) {
            ERROR("Could not create worker thread!\n");
         }
         tvalid[i]= 1;
      }
   }
   if (fflush(0)) write_error: ERROR(write_error_msg);
   goto cleanup;
   fail:
   (void)fprintf(
         stderr, "%s failed: %s\n"
      ,  argc ? argv[0] : "<unnamed program>", error
   );
   cleanup:
   if (have.tvalid) {
      unsigned i;
      for (i= threads; i--; ) {
         if (tvalid[i]) {
            tvalid[i]= 0;
            if (error) {
               if (pthread_cancel(tid[i])) {
                  ERROR("Could not terminate child thread!\n");
               }
            }
            {
               void *thread_error;
               if (pthread_join(tid[i], &thread_error)) {
                  ERROR("Failure waiting for child thread to terminate!");
               }
               if (thread_error && thread_error != PTHREAD_CANCELED) {
                  ERROR(thread_error);
               }
            }
         }
      }
      have.tvalid= 0; free(tvalid);
   }
   if (have.tid) {
      have.tid= 0; free(tid);
   }
   {
      unsigned i;
      for (i= (unsigned)DIM(tgs.shared_buffers); i--; ) {
         if (tgs.shared_buffers[i]) {
            void *old= tgs.shared_buffers[i]; tgs.shared_buffers[i]= 0;
            if (munmap(old, shared_buffer_size)) {
               unlikely_error:
               ERROR(exotic_error_msg);
            }
         }
      }
   }
   if (have.workers_mutex) {
      have.workers_mutex= 0;
      if (pthread_mutex_destroy(&tgs.workers_mutex)) goto unlikely_error;
   }
   if (have.io_mutex) {
      have.io_mutex= 0;
      if (pthread_mutex_destroy(&tgs.io_mutex)) goto unlikely_error;
   }
   if (have.workers_wakeup_call) {
      have.workers_wakeup_call= 0;
      if (pthread_cond_destroy(&tgs.workers_wakeup_call)) goto unlikely_error;
   }
   return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
