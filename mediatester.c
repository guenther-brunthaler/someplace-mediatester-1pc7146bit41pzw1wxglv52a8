/* Media tester
 *
 * Fills a block device or data stream with reprocucible pseudorandom bytes or
 * reads such a block device or data stream to verify the same pseudorandom
 * bytes written before are still present. Both write and verification mode
 * try to make use of the available CPU cores to create the pseudorandom data
 * data as quickly as possible in parallel, and double buffering is employed
 * to allow disk I/O to run (mostly) in parallel, too. */

#define VERSION_INFO \
 "Version 2019.79.1\n" \
 "Copyright (c) 2017-2019 Guenther Brunthaler. All rights reserved.\n" \
 "\n" \
 "This program is free software.\n" \
 "Distribution is permitted under the terms of the GPLv3."

#if !defined __STDC_VERSION__ || __STDC_VERSION__ < 199901
   #error "This source file requires a C99 compliant C compiler!"
#endif

#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS < 64
   #undef _FILE_OFFSET_BITS
#endif
#ifndef _FILE_OFFSET_BITS
   /* Feature-test macro which, if supported by the platform (such as glibc on
    * 32-bit Linux), will make lseek() and off_t support 64 bit offsets. */
   #define _FILE_OFFSET_BITS 64
#endif

#include <dim_sdbrke8ae851uitgzm4nv3ea2.h>
#include <pearson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

#ifndef MAP_ANONYMOUS
   #error "MAP_ANONYMOUS is not defined by <sys/mman.h>!" \
      "For Linux, add '-D _GNU_SOURCE' to C preprocessor flags." \
      "For BSD, add '-D _BSD_SOURCE' to fix the problem." \
      "For other OSes, examine yourself what '-D'-options to add."
#endif

/* TWO such buffers will be allocated. */
#define APPROXIMATE_BUFFER_SIZE (16ul << 20)

static struct {
   int read_mode, shutdown_requested /* = 0; */;
   uint8_t *shared_buffer, *shared_buffers[2];
   uint8_t const *shared_buffer_stop;
   size_t blksz /* = 0; */;
   size_t work_segments;
   size_t work_segment_sz;
   size_t shared_buffer_size;
   uint_fast64_t pos; /* Current position. */
   uint_fast64_t start_pos; /* Initial starting offset. */
   uint_fast64_t first_error_pos; /* Only valid if num_errors != 0. */
   uint_fast64_t last_error_pos; /* Only valid if num_errors != 0. */
   uint_fast64_t num_errors; /* Count of differing bytes. */
   unsigned active_threads /* = 0; */;
   pthread_mutex_t workers_mutex;
   pthread_cond_t workers_wakeup_call;
} tgs; /* Thread global storage */

#define CEIL_DIV(num, den) (((num) + (den) - 1) / (den))

#define ERROR(msg) { error= msg; goto fail; }
#define ERROR_CHECK() { if (error) goto fail; }

static char const msg_exotic_error[]= {
   "Unexpected error! (This should normally never happen.)"
};

static char const msg_write_error[]= {"Write error!"};

/* We *could* pass &tgs as the argument, but there is no point because there
 * exists only one such instance anyway. Also, accessing global variables is
 * typically faster than accessing variables indirectly via a pointer. */
static void *thread_func(void *unused_dummy) {
   char const *error, *first_error= 0;
   struct { unsigned workers_mutex_procured; } have= {0};
   (void)unused_dummy;
   /* Lock the mutex before acessing the global work state variables. */
   if (pthread_mutex_lock(&tgs.workers_mutex)) {
      lock_error:
      ERROR("Could not lock mutex!");
   }
   have.workers_mutex_procured= 1;
   ++tgs.active_threads; /* We just woke up (or have started). */
   /* Thread main loop. */
   for (;;) {
      assert(have.workers_mutex_procured);
      assert(tgs.active_threads >= 1);
      if (tgs.shutdown_requested) {
         shutdown:
         --tgs.active_threads;
         goto cleanup;
      }
      if (tgs.shared_buffer == tgs.shared_buffer_stop) {
         /* All worker segments have already been assigned to some thread. */
         if (tgs.active_threads == 1) {
            /* And we are the first/last thread running! Let's do I/O then. */
            if (tgs.read_mode) {
               #if 1
                  ERROR("Verify mode has not been implemented yet!");
               #else
               uint8_t *in, *next;
               size_t left;
               /* Switch buffers so other threads can resume working. */
               if (
                     tgs.shared_buffer - (left= tgs.shared_buffer_size)
                  == tgs.shared_buffers[0]
               ) {
                  next= tgs.shared_buffers[1];
               } else {
                  assert(tgs.shared_buffer - left == tgs.shared_buffers[1]);
                  next= tgs.shared_buffers[0];
               }
               //tgs.shared_buffer_stop= tgs.shared_buffer + left;
               have.workers_mutex_procured= 0;
               if (pthread_mutex_unlock(&tgs.workers_mutex)) goto unlock_error;
               /* Wake up other threads so they can start working on the other
                * buffer. */
               if (pthread_cond_broadcast(&tgs.workers_wakeup_call)) {
                  goto wakeup_error;
               }
               /* Write the old buffer while the other threads already fill
                * the new buffer. */
               for (;;) {
                  ssize_t written;
                  if ((written= write(1, out, left)) <= 0) {
                     if (written == 0) break;
                     if (written != -1) goto unlikely_error;
                     /* The write() has failed. Examine why. */
                     switch (errno) {
                        case ENOSPC: /* We filled up the filesystem. */
                        case EPIPE: /* Output stream has ended. */
                        case EDQUOT: /* Quota has been reached. */
                        case EFBIG: /* Maximum output size reached. */
                           /* Those are all considered "good" reasons why the
                            * write() has failed. */
                           assert(left > 0);
                           goto finished;
                        case EINTR: continue; /* Interrupted write(). */
                     }
                     ERROR(msg_write_error);
                  }
                  if ((size_t)written > left) goto unlikely_error;
                  out+= (size_t)written;
                  left-= (size_t)written;
               }
               finished:
               if (pthread_mutex_lock(&tgs.workers_mutex)) goto lock_error;
               have.workers_mutex_procured= 1;
               if (left) {
                  /* Output data sink does not accept any more data - we are
                   * done. Initiate successful termination. */
                  tgs.shutdown_requested= 1;
                  /* Make sure any sleeping threads will wake up to learn
                   * about the shutdown request. */
                  if (pthread_cond_broadcast(&tgs.workers_wakeup_call)) {
                     goto wakeup_error;
                  }
                  goto shutdown;
               }
               #endif
            } else {
               /* In write mode, we switch the buffer first, and let then the
                * other threads fill the next buffer while we write out the
                * old buffer's contents. */
               uint8_t const *out;
               size_t left;
               /* Switch buffers so other threads can resume working. */
               if (
                     tgs.shared_buffer - (left= tgs.shared_buffer_size)
                  == tgs.shared_buffers[0]
               ) {
                  out= tgs.shared_buffers[0];
                  tgs.shared_buffer= tgs.shared_buffers[1];
               } else {
                  out= tgs.shared_buffers[1];
                  assert(tgs.shared_buffer - left == out);
                  tgs.shared_buffer= tgs.shared_buffers[0];
               }
               tgs.shared_buffer_stop= tgs.shared_buffer + left;
               have.workers_mutex_procured= 0;
               if (pthread_mutex_unlock(&tgs.workers_mutex)) goto unlock_error;
               /* Wake up other threads so they can start working on the other
                * buffer. */
               if (pthread_cond_broadcast(&tgs.workers_wakeup_call)) {
                  wakeup_error:
                  ERROR("Could not wake up worker threads!");
               }
               /* Write the old buffer while the other threads already fill
                * the new buffer. */
               for (;;) {
                  ssize_t written;
                  if ((written= write(1, out, left)) <= 0) {
                     if (written == 0) break;
                     if (written != -1) {
                        unlikely_error: ERROR(msg_exotic_error);
                     }
                     /* The write() has failed. Examine why. */
                     switch (errno) {
                        case ENOSPC: /* We filled up the filesystem. */
                        case EPIPE: /* Output stream has ended. */
                        case EDQUOT: /* Quota has been reached. */
                        case EFBIG: /* Maximum output size reached. */
                           /* Those are all considered "good" reasons why the
                            * write() has failed. */
                           assert(left > 0);
                           goto finished;
                        case EINTR: continue; /* Interrupted write(). */
                     }
                     ERROR(msg_write_error);
                  }
                  if ((size_t)written > left) goto unlikely_error;
                  out+= (size_t)written;
                  left-= (size_t)written;
               }
               finished:
               if (pthread_mutex_lock(&tgs.workers_mutex)) goto lock_error;
               have.workers_mutex_procured= 1;
               if (left) {
                  /* Output data sink does not accept any more data - we are
                   * done. Initiate successful termination. */
                  tgs.shutdown_requested= 1;
                  /* Make sure any sleeping threads will wake up to learn
                   * about the shutdown request. */
                  if (pthread_cond_broadcast(&tgs.workers_wakeup_call)) {
                     goto wakeup_error;
                  }
                  goto shutdown;
               }
            }
         } else {
            assert(tgs.active_threads >= 2);
            /* We have nothing to do, but other worker threads are still
             * active. Just wait until there is again possibly something to
             * do. */
            --tgs.active_threads;
            have.workers_mutex_procured= 0;
            if (
               pthread_cond_wait(&tgs.workers_wakeup_call, &tgs.workers_mutex)
            ) {
               ERROR("Could not wait for condition variable!");
            }
            have.workers_mutex_procured= 1;
            ++tgs.active_threads;
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
         have.workers_mutex_procured= 1;
      }
   }
   fail:
   if (!first_error) first_error= error;
   cleanup:
   if (have.workers_mutex_procured) {
      have.workers_mutex_procured= 0;
      if (pthread_mutex_unlock(&tgs.workers_mutex)) {
         unlock_error:
         ERROR("Could not unlock mutex!");
      }
   }
   return (void *)first_error;
}
              
static uint_fast64_t atou64(char const **error, char const *numeric) {
   uint_fast64_t result;
   int converted;
   if (
         sscanf(numeric, "%" SCNuFAST64 "%n", &result, &converted) != 1
      || (size_t)converted != strlen(numeric)
   ) {
      *error= "Invalid number!";
      return ~UINT64_C(0) / 3;  /* Should not be used at all by caller! */
   }
   return result;
}

static void load_seed(char const **error, char const *seed_file) {
   #define MAX_SEED_BYTES 256
   char seed[MAX_SEED_BYTES + 1]; /* 1 byte larger than actually allowed. */
   size_t read;
   FILE *fh;
   #define error *error
   if (!(fh= fopen(seed_file, "rb"))) rd_err: ERROR("Cannot read seed file!");
   if ((read= fread(seed, sizeof *seed, DIM(seed), fh)) == DIM(seed)) {
      ERROR("Key file for seeding the PRNG is larger than supported!");
   }
   if (ferror(fh)) goto rd_err;
   assert(feof(fh));
   if (!read) ERROR("Seed file must not be empty!");
   pearnd_init(seed, read);
   fail:
   if (fh) {
      FILE *old= fh; fh= 0;
      if (fclose(old)) ERROR(msg_exotic_error);
   }
   #undef error
}

int main(int argc, char **argv) {
   char const *error= 0;
   unsigned threads;
   pthread_t *tid;
   char *tvalid;
   struct {
      unsigned tvalid: 1;
      unsigned tid: 1;
      unsigned workers_mutex: 1;
      unsigned workers_wakeup_call: 1;
   } have= {0};
   /* Ignore SIGPIPE because we want it as a possible errno from write(). */
   if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) goto unlikely_error;
   /* Preset global variables for interthread communication. */
   tgs.work_segments= 64;
   if (pthread_mutex_init(&tgs.workers_mutex, 0)) goto unlikely_error;
   have.workers_mutex= 1;
   if (pthread_cond_init(&tgs.workers_wakeup_call, 0)) goto unlikely_error;
   have.workers_wakeup_call= 1;
   /* Process arguments. */
   if (argc < 3 || argc > 4) {
      bad_arguments:
      ERROR(
         "Invalid arguments!\n"
         "\n"
         "Arguments: <mode> <seed_file> [ <starting_offset> ]\n"
         "\n"
         "where\n"
         "\n"
         "<mode>: One of the following commands\n"
         "  write - write PRNG stream to standard output at offset\n"
         "  verify - compare PRNG data against stream from standard input\n"
         "  compare - Like verify but show every byte ('should' and 'is')\n"
         "<seed_file>: a binary (or text) file up to 256 bytes PRNG seed\n"
         "<starting_offset>: byte offset where to start writing/verifying\n"
         "\n"
         "Standard input or output should be a block device or a file. When\n"
         "writing to a file, writing stops when there is no more free space\n"
         "left in the filesystem containing the file, or when the file has\n"
         "reached the maximum file size supported by the filesystem.\n"
         "\n"
         "The <seed_file> determines which pseudo-random sequence of bytes\n"
         "will be written to or will be expected to be read from the file\n"
         "or device. The same <seed_file> needs to be used for a 'write'\n"
         "command and its matching 'verify' command.\n"
         "\n"
         "The contents of <seed_file> are arbitrary and should be created\n"
         "by something like this:\n"
         "\n"
         "$ dd if=/dev/random bs=1 count=16 > my_seed_file.bin\n"
         "\n"
         "General usage procedure:\n"
         "\n"
         "1. Generate a seed file to be used with all following steps\n"
         "\n"
         "2. Fill the block device to be tested using the 'write' command\n"
         "\n"
         "   'write' can also fill a filesystem, using up all space. This\n"
         "   allows to check how much uncompressible data can really be\n"
         "   stored there, and whether the file has actually been written\n"
         "   correctly (i. e. whether the filesystem works as it should).\n"
         "\n"
         "3. Use 'verify' command to check for differences\n"
         "\n"
         "   This reads back the data which has been written before and\n"
         "   checks whether it is still the same. This should be the case\n"
         "   if everything is fine.\n"
         "   \n"
         "   If it is not, 'verify' stops and reports the offset where the\n"
         "   first different byte was detected.\n"
         "   \n"
         "   Differences mean that the storage medium or filesystem either\n"
         "   does not reliably write data, or it does not reliably read\n"
         "   data, or it is lying about its supposed storage capacity.\n"
         "   Either way, such differences are always a bad sign.\n"
         "   \n"
         "   To scan for more differences, you can run the 'verify' command\n"
         "   again, starting at a higher byte offset.\n"
         "\n"
         "4. If 'verify' stopped at differences, use 'compare' to show them\n"
         "\n"
         "   For every byte offset, 'compare' shows the byte value which\n"
         "   was expected, the byte value actually read back, the ASCII\n"
         "   character corresponding to the value read back, and the bit\n"
         "   differences (XOR) between both bytes.\n"
         "   \n"
         "   'compare' does not stop output by itself before the end of the\n"
         "   file/device has been reached. Its output should therefore be\n"
         "   piped through 'head' or 'more' in order to limit the number of\n"
         "   lines displayed.\n"
         "\n"
         VERSION_INFO
      );
   }
   if (!strcmp(argv[1], "write")) tgs.read_mode= 0;
   else if (!strcmp(argv[1], "verify")) tgs.read_mode= 1;
   else goto bad_arguments;
   /* Determine the best I/O block size, defaulting the value preset
    * earlier. */
   {
      struct stat st;
      int fd;
      mode_t mode;
      if (fstat(fd= !tgs.read_mode, &st)) {
         ERROR("Cannot examine file descriptor to be used for I/O!");
      }
      if (S_ISBLK(mode= st.st_mode)) {
         /* It's a block device. */
         {
            long logical;
            if (ioctl(fd, BLKSSZGET, &logical) < 0) {
               ERROR("Unable to determine logical sector size!");
            }
            if ((size_t)logical > tgs.blksz) tgs.blksz= (size_t)logical;
         }
         {
            long physical;
            if (ioctl(fd, BLKPBSZGET, &physical) < 0) {
               ERROR("Unable to determine physical sector size!");
            }
            if ((size_t)physical > tgs.blksz) tgs.blksz= (size_t)physical;
         }
         {
            long optimal;
            if (ioctl(fd, BLKIOOPT, &optimal) < 0) {
               ERROR("Unable to determine optimal sector size!");
            }
            if ((size_t)optimal > tgs.blksz) tgs.blksz= (size_t)optimal;
         }
      } else {
         /* Some other kind of data source/sink. Assume the maximum of the MMU
          * page size, the atomic pipe size and the fallback value. */
          long page_size;
          if ((page_size= sysconf(_SC_PAGESIZE)) == -1) goto unlikely_error;
          if ((size_t)page_size > tgs.blksz) tgs.blksz= (size_t)page_size;
          if (PIPE_BUF > tgs.blksz) tgs.blksz= PIPE_BUF;
      }
   }
   {
      size_t bmask= 512; /* <blksz> must be a power of 2 >= this value. */
      while (bmask < tgs.blksz) {
         size_t nmask;
         if (!((nmask= bmask + bmask) > bmask)) {
            ERROR("Could not determine a suitable I/O block size");
         }
         bmask= nmask;
      }
      tgs.blksz= bmask;
   }
   load_seed(&error, argv[2]); ERROR_CHECK();
   if (argc == 4) {
      tgs.start_pos= tgs.pos= atou64(&error, argv[3]); ERROR_CHECK();
      if (tgs.pos % tgs.blksz) {
         ERROR("Starting offset must be a multiple of the I/O block size!");
      }
      if ((off_t)tgs.pos < 0) ERROR("Numeric overflow in offset!");
      if (
         lseek(
               tgs.read_mode ? STDIN_FILENO : STDOUT_FILENO
            ,  (off_t)tgs.pos, SEEK_SET
         ) == (off_t)-1
      ) {
         ERROR("Could not reposition standard stream to starting position!");
      }
      #ifndef NDEBUG
         {
            off_t pos;
            if (
               (
                  pos= lseek(
                        tgs.read_mode ? STDIN_FILENO : STDOUT_FILENO
                     ,  (off_t)0, SEEK_CUR
                  )
               ) == (off_t)-1
            ) {
               ERROR("Could not determine standard stream position!");
            }
            assert(pos > 0);
            assert((uint_fast64_t)pos == tgs.pos);
         }
      #endif
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
   tgs.shared_buffer_size= tgs.work_segment_sz * tgs.work_segments;
   if (
      fprintf(
            stderr
         ,  "Starting %s offset: %" PRIdFAST64 " bytes\n"
            "Optimum device I/O block size: %u\n"
            "PRNG worker threads: %u\n"
            "worker's buffer segment size: %zu bytes\n"
            "number of worker segments: %zu\n"
            "size of buffer subdivided into worker segments: %zu bytes\n"
            "number of such buffers: %u\n"
            "\n%s PRNG data %s...\n"
         ,  tgs.read_mode ? "input" : "output"
         ,  tgs.pos
         ,  (unsigned)tgs.blksz
         ,  threads - 1
         ,  tgs.work_segment_sz
         ,  tgs.work_segments
         ,  tgs.shared_buffer_size
         ,  (unsigned)DIM(tgs.shared_buffers)
         ,  tgs.read_mode ? "reading" : "writing"
         ,  tgs.read_mode ? "from standard input" : "to standard output"
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
                     0, tgs.shared_buffer_size, PROT_READ | PROT_WRITE
                  ,  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
               )
            ) == MAP_FAILED
         ) {
            ERROR("Could not allocate I/O buffer!");
         }
      }
   }
   tgs.shared_buffer_stop=
      (tgs.shared_buffer= tgs.shared_buffers[0]) + tgs.shared_buffer_size
   ;
   /* In verify mode, we start with a "finished" buffer, forcing the next
    * buffer to be read as the first worker thread action. */
   if (tgs.read_mode) tgs.shared_buffer= (void *)tgs.shared_buffer_stop;
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
   if (fflush(0)) write_error: ERROR(msg_write_error);
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
            if (munmap(old, tgs.shared_buffer_size)) {
               unlikely_error:
               ERROR(msg_exotic_error);
            }
         }
      }
   }
   if (have.workers_mutex) {
      have.workers_mutex= 0;
      if (pthread_mutex_destroy(&tgs.workers_mutex)) goto unlikely_error;
   }
   if (have.workers_wakeup_call) {
      have.workers_wakeup_call= 0;
      if (pthread_cond_destroy(&tgs.workers_wakeup_call)) goto unlikely_error;
   }
   return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
