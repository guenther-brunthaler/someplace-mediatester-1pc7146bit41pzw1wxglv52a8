/* Media tester
 *
 * Fills a block device or data stream with reprocucible pseudorandom bytes or
 * reads such a block device or data stream to verify the same pseudorandom
 * bytes written before are still present. Both write and verification mode
 * try to make use of the available CPU cores to create the pseudorandom data
 * data as quickly as possible in parallel, and double buffering is employed
 * to allow disk I/O to run (mostly) in parallel, too. */

#define VERSION_INFO \
 "Version 2019.90.6\n" \
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
#include <getopt_nh7lll77vb62ycgwzwf30zlln.h>
#include <pearson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/ioprio.h>

#ifndef MAP_ANONYMOUS
   #error "MAP_ANONYMOUS is not defined by <sys/mman.h>!" \
      "For Linux, add '-D _GNU_SOURCE' to C preprocessor flags." \
      "For BSD, add '-D _BSD_SOURCE' to fix the problem." \
      "For other OSes, examine yourself what '-D'-options to add."
#endif

#ifndef SYS_ioprio_set
   #error "SYS_ioprio_set is not defined by <sys/syscall.h>!" \
      "For Linux, add '-D _GNU_SOURCE' to C preprocessor flags."
#endif

/* TWO such buffers will be allocated. */
#define APPROXIMATE_BUFFER_SIZE (16ul << 20)

/* Global variables, grouped in a struct for easier tracking. */
static struct {
   enum {
      mode_write, mode_verify, mode_compare, mode_diff
   } mode;
   int shutdown_requested /* = 0; */;
   uint8_t *shared_buffer, *shared_buffers[2];
   uint8_t const *shared_buffer_stop;
   size_t blksz /* = 0; */;
   size_t work_segments;
   size_t work_segment_sz;
   size_t shared_buffer_size;
   uint_fast64_t pos; /* Current position for next working segment. */
   uint_fast64_t start_pos; /* Initial starting offset. */
   uint_fast64_t first_error_pos; /* Only valid if num_errors != 0. */
   uint_fast32_t num_errors; /* Count of differing bytes. */
   unsigned active_threads; /* Number of threads not waiting for more work. */
   pthread_mutex_t workers_mutex; /* Serialize access to THIS struct. */
   pthread_cond_t workers_wakeup_call; /* Wake up threads for more work. */
   pthread_key_t resource_context; /* For r4g_c1(). */
} tgs; /* Thread global storage */

/* This is the primary type alias for working with the struct. No one wants to
 * use its overly long real name! */
typedef struct resource_context_4th_generation r4g;

/* Pointer to a destructor function which will be called for deallocating the
 * associated resource in rc->rlist. The destructor must replace rc->rlist
 * with a pointer to the next resource before actually destroying the current
 * resource, except for the case of a container object which is not yet empty.
 * In the latter case, the destructor should select the oldest element of the
 * container, remove it from the container first, and then deallocate only
 * that element. */
typedef void (*r4g_dtor)(r4g *rc);

/* Per-thread context for resource cleanup including error handling and
 * process termination. Associated with <tgs.resource_context>. */
struct resource_context_4th_generation {
   int rollback; /* Zero for committing transactions during cleanup. */
   char const *static_error_message; /* Null if not set. */
   /* Null if the resource list is empty, or the address within the last
    * resource list entry where the pointer to its associated destructor
    * function is stored. Destructors need to locate the resource to be
    * destroyed using this address. */
   r4g_dtor *rlist;
};

/* Defines a pointer variable to type resource_t and initializes it with a
 * pointer to the beginning of an object of type resource_t where rc->rlist
 * is a pointer to the struct's component <dtor_member>, which must be a
 * pointer to the destructor function for the object. <var_eq> must be the
 * part of the variable definition after resource_t and before the
 * initialization value, such as "* my_ptr=".
 *
 * Example: R4G_DEFINE_INIT_RPTR(struct my_resource, *r=, rc, dtor); */
#define R4G_DEFINE_INIT_RPTR(resource_t, var_eq, r4g_rc, dtor_member) \
   resource_t var_eq (void *)( \
      (char *)(r4g_rc)->rlist - offsetof(resource_t, dtor_member) \
   )

static char const msg_malloc_error[]= {
   "Memory allocation failure!"
};

static char const *new_thread_r4g(void) {
   r4g *rc;
   if (!(rc= calloc(1, sizeof *rc))) return msg_malloc_error;
   if (!pthread_setspecific(tgs.resource_context, rc)) return 0;
   return "Could not set up per-thread resource context!";
}

/* Calls the destructor of the last entry in the specified resource list until
 * there are no more entries left. Destructors need to unlink their entries
 * from the resource list eventually, or this will become an endless loop.
 * Destructors are also free to abort this loop prematurely by performing
 * non-local jumps or calling exit(). */
static void release_c1(r4g *resources) {
   while (resources->rlist) (*resources->rlist)(resources);
}

/* Like release_c1() but stop releasing when resource <stop_at> would be
 * released next. */
static void release_to_c1(r4g *resources, r4g_dtor *stop_at) {
   r4g_dtor *r;
   while ((r= resources->rlist) && r != stop_at) (*r)(resources);
}

/* Raise an error in the specified resource context. <emsg> is a statically
 * allocated text message, which will then be made available as the current
 * error message of the resource context. However, this will be skipped and
 * <emsg> will be completely ignored if <rollback> of the resource context is
 * not zero, which means error processing is already underway and this is just
 * a follow-up error. Next, <rollback> will be set to non-zero. Then
 * release_c1() is called to release all resources, which will also display
 * the error message if an appropriate resource destructor for this purpose
 * has been allocated in the resource list before. Finally, the application
 * will be terminated by exiting with a return code of EXIT_FAILURE. If a
 * different return code is preferred, a resource for this purpose must be
 * allocated, and its destructor should call exit() with the required return
 * code. */
static void error_w_context_c1(r4g *rc, char const *emsg) {
   if (!rc->rollback) {
      rc->static_error_message= emsg;
      rc->rollback= 1;
   }
   release_c1(rc);
   exit(EXIT_FAILURE);
}

/* Returns the current resource context of the executing thread. Depending on
 * the build configuration, this may be retrieved from an internal static or
 * thread-local variable. It may abort() in a multithreaded configuration if
 * the thread-local resource context cannot be retrieved. */
static r4g *r4g_c1(void) {
   r4g *rc;
   if (rc= pthread_getspecific(tgs.resource_context)) return rc;
   abort();
}

/* Same as error_w_context_c1(), but uses the current thread's local resource
 * context. */
static void error_c1(char const *emsg) {
   error_w_context_c1(r4g_c1(), emsg);
}

static char const msg_exotic_error[]= {
   "Unexpected error! (This should normally never happen.)"
};

static char const msg_write_error[]= {"Write error!"};

static void pthread_mutex_lock_c1(pthread_mutex_t *mutex) {
   if (!pthread_mutex_lock(mutex)) return;
   error_c1("Could not lock mutex!");
}

static void pthread_cond_broadcast_c1(pthread_cond_t *cond) {
   if (!pthread_cond_broadcast(cond)) return;
   error_c1("Could not wake up worker threads!");
}

static void pthread_cond_wait_c1(
   pthread_cond_t *restrict cond, pthread_mutex_t *restrict mutex
) {
   if (!pthread_cond_wait(cond, mutex)) return;
   error_c1("Could not wait for condition variable!");
}

static void pthread_mutex_unlock_c1(pthread_mutex_t *mutex) {
   if (!pthread_mutex_unlock(mutex)) return;
   error_c1("Could not unlock mutex!");
}

static void *malloc_c1(size_t bytes) {
   void *buffer;
   if (!(buffer= malloc(bytes))) error_c1(msg_malloc_error);
   return buffer;
}

static void printf_c1(char const *format, ...) {
   va_list args;
   int error;
   va_start(args, format);
   error= vprintf(format, args) < 0;
   va_end(args);
   if (error) error_c1(msg_write_error);
}

static void fprintf_c1(FILE *s, char const *format, ...) {
   va_list args;
   int error;
   va_start(args, format);
   error= vfprintf(s, format, args) < 0;
   va_end(args);
   if (error) error_c1(msg_write_error);
}

struct mutex_resource {
   int procured;
   pthread_mutex_t *mutex;
   r4g_dtor dtor, *saved;
};

static void mutex_unlocker_dtor(r4g *rc) {
   R4G_DEFINE_INIT_RPTR(struct mutex_resource, *r=, rc, dtor);
   int procured= r->procured;
   pthread_mutex_t *mutex= r->mutex;
   rc->rlist= r->saved;
   free(r);
   if (procured) pthread_mutex_unlock_c1(mutex);
}

/* Returns the address of a status variable which must reflect the current
 * locking state of the mutex. Zero means unlocked (and is also the initial
 * setting). The caller needs to update this status in order to track the
 * current state of the mutex. A resource is added to the current resource
 * list which will unlock the mutex when the status indicates it is currently
 * locked at the time when the destructor of the resource will be invoked. */
static int *mutex_unlocker_c5(pthread_mutex_t *mutex) {
   r4g *rc= r4g_c1();
   struct mutex_resource *r= malloc_c1(sizeof *r);
   r->procured= 0;
   r->mutex= mutex;
   r->saved= rc->rlist; r->dtor= &mutex_unlocker_dtor; rc->rlist= &r->dtor;
   return &r->procured;
}

static void *writer_thread(void *unused_dummy) {
   r4g *rc;
   int *workers_mutex_procured;
   (void)unused_dummy;
   {
      char const *error;
      if (error= new_thread_r4g()) return (void *)error;
   }
   rc= r4g_c1();
   workers_mutex_procured= mutex_unlocker_c5(&tgs.workers_mutex);
   assert(!*workers_mutex_procured);
   /* Lock the mutex before acessing the global work state variables. */
   pthread_mutex_lock_c1(&tgs.workers_mutex);
   *workers_mutex_procured= 1;
   ++tgs.active_threads; /* We just have started. */
   /* Thread main loop. */
   for (;;) {
      assert(*workers_mutex_procured);
      assert(tgs.active_threads >= 1);
      if (tgs.shutdown_requested) {
         shutdown:
         --tgs.active_threads;
         break;
      }
      if (tgs.shared_buffer == tgs.shared_buffer_stop) {
         /* All worker segments have already been assigned to some
          * thread. */
         if (tgs.active_threads == 1) {
            /* And we are the first/last thread running! Let's do I/O then.
             * We switch the buffer first, and let then the other threads
             * fill the next buffer while we write out the old buffer's
             * contents. */
            uint8_t const *out;
            size_t left;
            uint_fast64_t pos;
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
            pos= tgs.pos - left;
            tgs.shared_buffer_stop= tgs.shared_buffer + left;
            assert(*workers_mutex_procured);
            *workers_mutex_procured= 0;
            pthread_mutex_unlock_c1(&tgs.workers_mutex);
            /* Wake up the other threads so they can start working on the
             * other buffer. */
            pthread_cond_broadcast_c1(&tgs.workers_wakeup_call);
            /* Write out the old buffer while the other threads already fill
             * the new buffer. */
            for (;;) {
               ssize_t written;
               if ((written= write(STDOUT_FILENO, out, left)) <= 0) {
                  if (written == 0) break;
                  if (written != -1) {
                     unlikely_error: error_c1(msg_exotic_error);
                  }
                  /* The write() has failed. Examine why. */
                  switch (errno) {
                     case ENOSPC: /* We filled up the filesystem. */
                     case EPIPE: /* Output stream has ended. */
                     case EDQUOT: /* Quota has been reached. */
                     case EFBIG: /* Maximum file/device size reached. */
                        /* Those are all considered "good" reasons why the
                         * write() has failed. */
                        assert(left > 0);
                        goto finished;
                     case EINTR: continue; /* Interrupted write(). */
                  }
                  assert(pos >= tgs.start_pos);
                  (void)fprintf(
                        stderr
                     ,  "Write error at byte offset %" PRIuFAST64 "!\n"
                        "(Output did start at byte offset %" PRIuFAST64 ")\n"
                        "Total bytes written so far: %" PRIuFAST64 "\n"
                     ,  pos, tgs.start_pos, pos - tgs.start_pos
                  );
                  error_c1(msg_write_error);
               }
               if ((size_t)written > left) goto unlikely_error;
               out+= (size_t)written;
               pos+= (uint_fast64_t)written;
               left-= (size_t)written;
            }
            finished:
            assert(!*workers_mutex_procured);
            pthread_mutex_lock_c1(&tgs.workers_mutex);
            *workers_mutex_procured= 1;
            if (left) {
               /* Output data sink does not accept any more data - we are
                * done. Try to write some statistics to standard error. */
               assert(pos >= tgs.start_pos);
               fprintf_c1(
                     stderr
                  ,  "\n"
                     "Success!\n"
                     "\n"
                     "Output stopped at byte offset %" PRIuFAST64 "!\n"
                     "(Output did start at byte offset %" PRIuFAST64 ")\n"
                     "Total bytes written: %" PRIuFAST64 "\n"
                  ,  pos, tgs.start_pos, pos - tgs.start_pos
               );
               /* Initiate successful termination. */
               tgs.shutdown_requested= 1;
               /* Make sure any sleeping threads will wake up to learn
                * about the shutdown request. */
               pthread_cond_broadcast_c1(&tgs.workers_wakeup_call);
               goto shutdown;
            }
         } else {
            assert(tgs.active_threads >= 2);
            /* We have nothing to do, but other worker threads are still
             * active. Just wait until there is again possibly something to
             * do. */
            --tgs.active_threads;
            assert(*workers_mutex_procured);
            *workers_mutex_procured= 0;
            /* Unlock mutex, wait for a broadcast, then lock mutex again. */
            pthread_cond_wait_c1(
               &tgs.workers_wakeup_call, &tgs.workers_mutex
            );
            *workers_mutex_procured= 1;
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
         *workers_mutex_procured= 0;
         pthread_mutex_unlock_c1(&tgs.workers_mutex);
         /* Do every worker thread's primary job: Process its work
          * segment. */
         pearnd_generate(work_segment, tgs.work_segment_sz, &po);
         /* See whether we can get the next job. */
         pthread_mutex_lock_c1(&tgs.workers_mutex);
         *workers_mutex_procured= 1;
      }
   }
   release_c1(rc);
   return (void *)rc->static_error_message;
}

static void *reader_thread(void *unused_dummy) {
   r4g *rc;
   int *workers_mutex_procured;
   (void)unused_dummy;
   {
      char const *error;
      if (error= new_thread_r4g()) return (void *)error;
   }
   rc= r4g_c1();
   workers_mutex_procured= mutex_unlocker_c5(&tgs.workers_mutex);
   assert(!*workers_mutex_procured);
   /* Lock the mutex before acessing the global work state variables. */
   pthread_mutex_lock_c1(&tgs.workers_mutex);
   *workers_mutex_procured= 1;
   ++tgs.active_threads; /* We just have started. */
   /* Thread main loop. */
   for (;;) {
      assert(*workers_mutex_procured);
      assert(tgs.active_threads >= 1);
      error_c1("Verify mode has not been implemented yet!");
   }
   release_c1(rc);
   return (void *)rc->static_error_message;
}
              
static uint_fast64_t atou64(char const *numeric) {
   uint_fast64_t result;
   int converted;
   if (
         sscanf(numeric, "%" SCNuFAST64 "%n", &result, &converted) != 1
      || (size_t)converted != strlen(numeric)
   ) {
      error_c1("Invalid number!");
   }
   return result;
}

struct FILE_mallocated_resource {
   FILE *handle;
   r4g_dtor dtor, *saved;
};

static void FILE_mallocated_dtor(r4g *rc) {
   R4G_DEFINE_INIT_RPTR(struct FILE_mallocated_resource, *r=, rc, dtor);
   FILE *fh= r->handle;
   rc->rlist= r->saved;
   free(r);
   if (fh && fclose(fh)) error_c1("Error while closing file!");
}

static void load_seed(char const *seed_file) {
   struct FILE_mallocated_resource *f= malloc_c1(sizeof *f);
   FILE *fh;
   r4g *rc;
   r4g_dtor *marker;
   f->saved= marker= (rc= r4g_c1())->rlist; f->dtor= &FILE_mallocated_dtor;
   rc->rlist= &f->dtor;
   if (!(f->handle= fh= fopen(seed_file, "rb"))) {
      rd_err: error_c1("Cannot read seed file!");
   }
   {
      #define MAX_SEED_BYTES 256
      char seed[MAX_SEED_BYTES + 1]; /* 1 byte larger than actually allowed. */
      #undef MAX_SEED_BYTES
      size_t read;
      if ((read= fread(seed, sizeof *seed, DIM(seed), fh)) == DIM(seed)) {
         error_c1("Key file for seeding the PRNG is larger than supported!");
      }
      if (ferror(fh)) goto rd_err;
      assert(feof(fh));
      if (!read) error_c1("Seed file must not be empty!");
      pearnd_init(seed, read);
   }
   release_to_c1(rc, marker);
}

static void slow_comparison(void) {
   uint_fast64_t differences= 0;
   uint_fast64_t pos;
   pearnd_offset po;
   uint8_t *const reference= tgs.shared_buffers[1];
   /* Write a header. */
   fprintf_c1(stderr, "\nEX RD A XOR BYTE_OFFSET\n");
   pearnd_seek(&po, pos= tgs.pos);
   for (;;) {
      uint8_t *in= tgs.shared_buffer;
      size_t left= tgs.shared_buffer_size;
      /* Read the next buffer full of input data. */
      for (;;) {
         ssize_t did_read;
         if ((did_read= read(STDIN_FILENO, in, left)) <= 0) {
            if (did_read == 0) break;
            if (did_read != -1) {
               unlikely_error: error_c1(msg_exotic_error);
            }
            /* The write() has failed. Examine why. */
            switch (errno) {
               case EFBIG: /* Maximum file/device size reached. */
                  /* This is considered a "good" reason why the read() has
                   * failed. */
                  assert(left > 0);
                  goto finished;
               case EINTR: continue; /* Interrupted read(). */
            }
            assert(pos >= tgs.start_pos);
            (void)fprintf(
                  stderr
               ,  "Read error at byte offset %" PRIuFAST64 "!\n"
                  "(Reading did start at byte offset %" PRIuFAST64 ")\n"
                  "Total bytes read so far: %" PRIuFAST64 "\n"
               ,  pos, tgs.start_pos, pos - tgs.start_pos
            );
            error_c1("Read error!");
         }
         if ((size_t)did_read > left) goto unlikely_error;
         in+= (size_t)did_read;
         pos+= (uint_fast64_t)did_read;
         left-= (size_t)did_read;
      }
      /* Generate a full buffer of comparison data. */
      pearnd_generate(reference, tgs.shared_buffer_size, &po);
      /* Compare buffer contents. */
      assert(left < tgs.shared_buffer_size);
      pos-= left= tgs.shared_buffer_size - left;
      assert(left >= 1);
      in= tgs.shared_buffer;
      {
         size_t i;
         for (i= 0; i < left; ++i) {
            if (tgs.mode == mode_diff && in[i] == reference[i]) continue;
            {
               char octet[8];
               unsigned rd= in[i];
               {
                  unsigned xor= rd ^ reference[i];
                  unsigned i, mask= 1;
                  for (i= 8; i--; mask+= mask) {
                     octet[i]= xor & mask ? '1' : '0';
                  }
                  if (xor) ++differences;
               }
               fprintf_c1(
                     stderr
                  ,  "%02X %02X %c %8s %" PRIuFAST64 "\n"
                  ,  (unsigned)reference[i], rd
                  ,  rd >= 0x20 && rd < 0x7f ? rd : '.'
                  ,  octet, pos + i
               );
            }
         }
      }
      pos+= left;
   }
   finished:
   assert(pos >= tgs.start_pos);
   fprintf_c1(
         stderr
      ,  "\n"
         "Comparison complete!\n"
         "\n"
         "Reading stopped at byte offset %" PRIuFAST64 "!\n"
         "(Reading did start at byte offset %" PRIuFAST64 ")\n"
         "Different bytes encountered: %" PRIuFAST64 "\n"
         "Total bytes compared: %" PRIuFAST64 "\n"
      ,  pos, tgs.start_pos, differences, pos - tgs.start_pos
   );
}

static char const usage[]=
   "Usage: %s [ <options> ... ] <mode> <seed_file> [ <starting_offset> ]\n"
   "\n"
   "where\n"
   "\n"
   "<mode>: One of the following commands\n"
   "  write - write PRNG stream to standard output at offset\n"
   "  verify - compare PRNG data against stream from standard input\n"
   "  compare - Like verify but show every byte ('should' and 'is')\n"
   "  diff - Like compare but report only differing bytes\n"
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
   "<options>:\n"
   "\n"
   "-t <n>: Use <n> CPU threads for write/verify commands instead of\n"
   "the autodetected number of available processor cores\n"
   "\n"
   "-N: Don't be nice. By default, the program will behave as if it\n"
   "had been invoked via 'nice' and 'ionice -c 3'. With -N, the\n"
   "program will not do this and keep its initial niceness settings.\n"
   "\n"
   "-F: Don't flush the device's cache when reading from a block\n"
   "device. This makes comparison of small block devices less\n"
   "reliable, because one never can be sure whether the data read\n"
   "came actually from the device or rather from the cache. However,\n"
   "flushing device buffers is a privileged operation, so\n"
   "unprivileged users can never read from a block device without\n"
   "this option, even if they are the owner of the device.\n"
   "\n"
   "-V: Display version information\n"
   "\n"
   "-h: Display this help\n"
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
   "4. If 'verify' stopped at differences, use 'compare' or 'diff'\n"
   "   to show them.\n"
   "\n"
   "   For every byte offset, 'compare' shows the byte value which\n"
   "   was expected, the byte value actually read back, the ASCII\n"
   "   character corresponding to the value read back, and the bit\n"
   "   differences (XOR) between both bytes.\n"
   "   \n"
   "   'diff' does the same, but only writes output lines for bytes\n"
   "   which actually differ. (It will therefore never display the\n"
   "   XOR-value '00000000'.)\n"
   "   \n"
   "   This means 'diff' does basically the same as 'verify', but it\n"
   "   is much, much slower. And unlike 'verify', 'diff' will not\n"
   "   stop comparing when differences have been found.\n"
   "   \n"
   "   Therefore, always use 'verify' first to determine where bytes\n"
   "   start to differ, then use 'diff' or 'compare' to look what\n"
   "   exactly is different.\n"
   "   \n"
   "   'compare' and 'diff' do not stop output by themselves before\n"
   "   the end of the file/device has been reached. Their output\n"
   "   should therefore be piped through 'head' or 'more' in order\n"
   "   to limit the number of lines displayed.\n"
   "\n"
   VERSION_INFO "\n"
;

struct error_reporting_static_resource {
   r4g_dtor dtor, *saved;
   char const *argv0;
};

static void error_reporting_dtor(r4g *rc) {
   char const *em= 0;
   R4G_DEFINE_INIT_RPTR( \
      struct error_reporting_static_resource, *c=, rc, dtor \
   );
   rc->rlist= c->saved;
   if (rc->rollback && rc->static_error_message) {
      em= rc->static_error_message;
      rc->static_error_message= 0;
   }
   if (em) (void)fprintf(stderr, "%s failed: %s\n", c->argv0, em);
}

struct minimal_resource {
   r4g_dtor dtor, *saved;
};

static void resource_context_thread_key_dtor(r4g *rc) {
   R4G_DEFINE_INIT_RPTR(struct minimal_resource, *r=, rc, dtor);
   /* Clear out slot for main thread context because it has not been
    * allocated dynamically and should therefore not be passed to the
    * thread key's per-thread slot destructor. */
   int bad= pthread_setspecific(tgs.resource_context, 0);
   rc->rlist= r->saved;
   if (pthread_key_delete(tgs.resource_context) || bad) {
      error_w_context_c1(rc, msg_exotic_error);
   }
}

struct pthread_mutex_static_resource {
   pthread_mutex_t *mutex;
   r4g_dtor dtor, *saved;
};

static void pthread_mutex_static_dtor(r4g *rc) {
   R4G_DEFINE_INIT_RPTR(struct pthread_mutex_static_resource, *r=, rc, dtor);
   rc->rlist= r->saved;
   if (pthread_mutex_destroy(r->mutex)) error_c1(msg_exotic_error);
}

struct pthread_cond_static_resource {
   pthread_cond_t *cond;
   r4g_dtor dtor, *saved;
};

static void pthread_cond_static_dtor(r4g *rc) {
   R4G_DEFINE_INIT_RPTR(struct pthread_cond_static_resource, *r=, rc, dtor);
   rc->rlist= r->saved;
   if (pthread_cond_destroy(r->cond)) error_c1(msg_exotic_error);
}

static void shared_buffers_dtor(r4g *rc) {
   {
      unsigned i;
      for (i= (unsigned)DIM(tgs.shared_buffers); i--; ) {
         if (tgs.shared_buffers[i]) {
            void *old= tgs.shared_buffers[i]; tgs.shared_buffers[i]= 0;
            if (munmap(old, tgs.shared_buffer_size)) {
               error_c1(msg_exotic_error);
            }
         }
      }
   }
   {
      R4G_DEFINE_INIT_RPTR(struct minimal_resource, *r=, rc, dtor);
      rc->rlist= r->saved;
   }
}

struct cancel_threads_static_resource {
   unsigned threads;
   pthread_t *tid;
   char *tvalid;
   r4g_dtor dtor, *saved;
};

static void cancel_threads_dtor(r4g *rc) {
   R4G_DEFINE_INIT_RPTR(struct cancel_threads_static_resource, *r=, rc, dtor);
   {
      unsigned i;
      for (i= r->threads; i--; ) {
         if (r->tvalid[i]) {
            r->tvalid[i]= 0;
            if (rc->rollback) {
               if (pthread_cancel(r->tid[i])) {
                  error_c1("Could not terminate child thread!");
               }
            }
            {
               void *thread_error;
               if (pthread_join(r->tid[i], &thread_error)) {
                  error_c1("Failure waiting for child thread to terminate!");
               }
               if (thread_error && thread_error != PTHREAD_CANCELED) {
                  error_c1(thread_error);
               }
            }
         }
      }
   }
   rc->rlist= r->saved;
}

struct mallocated_resource {
   void *buffer;
   r4g_dtor dtor, *saved;
};

static void mallocated_dtor(r4g *rc) {
   R4G_DEFINE_INIT_RPTR(struct mallocated_resource, *r=, rc, dtor);
   void *buffer= r->buffer;
   rc->rlist= r->saved;
   free(r);
   free(buffer);
}

static void *calloc_c5(size_t num_elements, size_t element_size) {
   struct mallocated_resource *r= malloc_c1(sizeof *r);
   r4g *rc;
   r->saved= (rc= r4g_c1())->rlist;
   r->dtor= &mallocated_dtor; rc->rlist= &r->dtor;
   if (!(r->buffer= calloc(num_elements, element_size))) {
      error_c1(msg_malloc_error);
   }
   return r->buffer;
}

#define CEIL_DIV(num, den) (((num) + (den) - 1) / (den))

int main(int argc, char **argv) {
   static unsigned threads;
   static pthread_t *tid;
   static char *tvalid;
   static r4g m;
   char const *argv0;
   int never_flush= 0;
   {
      static struct error_reporting_static_resource r;
      r.argv0= argv0= argc ? argv[0] : "<unnamed program>";
      r.saved= m.rlist; r.dtor= &error_reporting_dtor; m.rlist= &r.dtor;
   }
   if (pthread_key_create(&tgs.resource_context, free)) {
      error_w_context_c1(
         &m, "Could not allocate a new slot for thread-local storage!"
      );
   }
   {
      static struct minimal_resource r;
      r.saved= m.rlist; r.dtor= &resource_context_thread_key_dtor;
      m.rlist= &r.dtor;
   }
   if (pthread_setspecific(tgs.resource_context, &m)) {
      error_w_context_c1(
         &m, "Could not set error-handling context for main thread!"
      );
   }
   {
      int optind;
      int be_nice= 1;
      {
         int opt, optpos;
         char const *optarg;
         optind= optpos= 0;
         while (opt= getopt_simplest(&optind, &optpos, argc, argv)) {
            switch (opt) {
               case 't':
                  if (
                     !(
                        optarg= getopt_simplest_mand_arg(
                           &optind, &optpos, argc, argv
                        )
                     )
                  ) {
                     getopt_simplest_perror_missing_arg(opt);
                     goto error_shown;
                  }
                  {
                     int converted;
                     if (
                           sscanf(
                              optarg, "%u%n", &threads, &converted
                           ) != 1
                        || (size_t)converted != strlen(optarg)
                     ) {
                        error_c1("Invalid numeric option argument!");
                     }
                  }
                  break;
               case 'N': be_nice= 0; break;
               case 'V': printf_c1("%s\n", VERSION_INFO); goto cleanup;
               case 'h': printf_c1(usage, argv0); goto cleanup;
               case 'F': never_flush= 1; break;
               default:
                  getopt_simplest_perror_opt(opt);
                  goto error_shown;
            }
         }
      }
      /* Process arguments. */
      if (optind == argc) goto bad_arguments;
      {
         char const *cmd;
         if (!strcmp(cmd= argv[optind++], "write")) tgs.mode= mode_write;
         else if (!strcmp(cmd, "verify")) tgs.mode= mode_verify;
         else if (!strcmp(cmd, "compare")) tgs.mode= mode_compare;
         else if (!strcmp(cmd, "diff")) tgs.mode= mode_diff;
         else goto bad_arguments;
      }
      if (optind == argc) goto bad_arguments;
      load_seed(argv[optind++]);
      if (optind < argc) {
         tgs.pos= atou64(argv[optind++]);
      }
      if (optind != argc) {
         bad_arguments:
         {
            FILE *out;
            {
               struct stat st;
               out=
                        isatty(STDOUT_FILENO)
                     || fstat(STDOUT_FILENO, &st) == 0 && S_ISFIFO(st.st_mode)
                  ?  stdout
                  :  stderr
               ;
            }
            (void)fprintf(out, "%s\n\n", "Invalid arguments!");
            (void)fprintf(out, usage, argv0);
         }
         error_shown:
         m.static_error_message= 0;
         m.rollback= 1;
         goto cleanup;
      }
      if (be_nice) {
         errno= 0;
         if (nice(10) == -1 && errno) {
            error_c1("Could not make the process nice in terms of CPU usage!");
         }
         if (
            syscall(
                  SYS_ioprio_set, IOPRIO_WHO_PROCESS, getpid()
               ,  IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0))
         ) {
            error_c1(
               "Could not make the process nice in terms of I/O priority!"
            );
         }
      }
   }
   /* Ignore SIGPIPE because we want it as a possible errno from write(). */
   if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) goto unlikely_error;
   /* Preset global variables for interthread communication. */
   tgs.work_segments= 64;
   if (pthread_mutex_init(&tgs.workers_mutex, 0)) goto unlikely_error;
   {
      static struct pthread_mutex_static_resource r;
      r.mutex= &tgs.workers_mutex;
      r.saved= m.rlist; r.dtor= &pthread_mutex_static_dtor;
      m.rlist= &r.dtor;
   }
   if (pthread_cond_init(&tgs.workers_wakeup_call, 0)) goto unlikely_error;
   {
      static struct pthread_cond_static_resource r;
      r.cond= &tgs.workers_wakeup_call;
      r.saved= m.rlist; r.dtor= &pthread_cond_static_dtor;
      m.rlist= &r.dtor;
   }
   /* Determine the best I/O block size, defaulting to the value preset
    * earlier. */
   {
      struct stat st;
      int fd;
      mode_t mode;
      if (
         fstat(
               fd= tgs.mode != mode_write ? STDIN_FILENO : STDOUT_FILENO
            ,  &st
         )
      ) {
         error_c1("Cannot examine file descriptor to be used for I/O!");
      }
      if (S_ISBLK(mode= st.st_mode)) {
         /* It's a block device. */
         {
            long logical;
            if (ioctl(fd, BLKSSZGET, &logical) < 0) {
               error_c1("Unable to determine logical sector size!");
            }
            if ((size_t)logical > tgs.blksz) tgs.blksz= (size_t)logical;
         }
         {
            long physical;
            if (ioctl(fd, BLKPBSZGET, &physical) < 0) {
               error_c1("Unable to determine physical sector size!");
            }
            if ((size_t)physical > tgs.blksz) tgs.blksz= (size_t)physical;
         }
         {
            long optimal;
            if (ioctl(fd, BLKIOOPT, &optimal) < 0) {
               error_c1("Unable to determine optimal sector size!");
            }
            if ((size_t)optimal > tgs.blksz) tgs.blksz= (size_t)optimal;
         }
         if (
            !never_flush && tgs.mode != mode_write && ioctl(fd, BLKFLSBUF) < 0
         ) {
             error_c1(
                "Unable to flush device buffer before starting operation!"
             );
         }
      } else {
         /* Some other kind of data source/sink. Assume the maximum of the
          * MMU page size, the atomic pipe size and the fallback value. */
         long page_size;
         if ((page_size= sysconf(_SC_PAGESIZE)) == -1) {
            unlikely_error: error_c1(msg_exotic_error);
         }
         if ((size_t)page_size > tgs.blksz) tgs.blksz= (size_t)page_size;
         if (PIPE_BUF > tgs.blksz) tgs.blksz= PIPE_BUF;
      }
   }
   {
      size_t bmask= 512; /* <blksz> must be a power of 2 >= this value. */
      while (bmask < tgs.blksz) {
         size_t nmask;
         if (!((nmask= bmask + bmask) > bmask)) {
            error_c1("Could not determine a suitable I/O block size");
         }
         bmask= nmask;
      }
      tgs.blksz= bmask;
   }
   if (tgs.start_pos= tgs.pos) {
      if (tgs.pos % tgs.blksz) {
         error_c1("Starting offset must be a multiple of the I/O block size!");
      }
      if ((off_t)tgs.pos < 0) error_c1("Numeric overflow in offset!");
      if (
         lseek(
               tgs.mode != mode_write ? STDIN_FILENO : STDOUT_FILENO
            ,  (off_t)tgs.pos, SEEK_SET
         ) == (off_t)-1
      ) {
         seeking_did_not_work:
         error_c1(
            "Could not reposition standard stream to starting position!"
         );
      }
      {
         off_t pos;
         if (
            (
               pos= lseek(
                        tgs.mode != mode_write
                     ?  STDIN_FILENO
                     :  STDOUT_FILENO
                  ,  (off_t)0, SEEK_CUR
               )
            ) == (off_t)-1
         ) {
            error_c1("Could not determine standard stream position!");
         }
         assert(pos >= 0);
         if ((uint_fast64_t)pos != tgs.pos) goto seeking_did_not_work;
      }
   }
   switch (tgs.mode) {
      case mode_compare:
      case mode_diff:
         threads= 1; 
         break;
      default:
      {
         long rc;
         unsigned procs;
         if (
               (rc= sysconf(_SC_NPROCESSORS_ONLN)) == -1
            || (procs= (unsigned)rc , (long)procs != rc)
         ) {
            error_c1(
               "Could not determine number of available CPU processors!"
            );
         }
         if (!threads || procs < threads) threads= procs;
      }
      if (threads < tgs.work_segments) {
         if (threads == 1) tgs.work_segments= 1;
         tgs.work_segments= tgs.work_segments / threads * threads;
         assert(tgs.work_segments >= 1);
      } else {
         tgs.work_segments= threads;
      }
      /* Most threads will generate PRNG data. Another one does I/O and
       * switches working buffers when the next buffer is ready. The main
       * program thread only waits for termination of the other threads. */
      ++threads; /* Compensate workers for lazy main program. */
   }
   tgs.work_segment_sz=
      CEIL_DIV(APPROXIMATE_BUFFER_SIZE, tgs.work_segments)
   ;
   tgs.work_segment_sz=
      CEIL_DIV(tgs.work_segment_sz, tgs.blksz) * tgs.blksz
   ;
   tgs.shared_buffer_size= tgs.work_segment_sz * tgs.work_segments;
   fprintf_c1(
         stderr
      ,  "Starting %s offset: %" PRIdFAST64 " bytes\n"
         "I/O block size: %u\n"
         "PRNG worker threads: %u\n"
         "worker's buffer segment size: %zu bytes\n"
         "number of worker segments: %zu\n"
         "size of buffer providing those worker segments: %zu bytes\n"
         "number of such buffers: %u\n"
         "\n%s PRNG data %s...\n"
      ,  tgs.mode != mode_write ? "input" : "output"
      ,  tgs.pos
      ,  (unsigned)tgs.blksz
      ,  threads - 1
      ,  tgs.work_segment_sz
      ,  tgs.work_segments
      ,  tgs.shared_buffer_size
      ,  (unsigned)DIM(tgs.shared_buffers)
      ,  tgs.mode != mode_write ? "reading" : "writing"
      ,  tgs.mode != mode_write
         ? "from standard input"
         : "to standard output"
   );
   {
      static struct minimal_resource r;
      r.saved= m.rlist; r.dtor= &shared_buffers_dtor; m.rlist= &r.dtor;
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
            error_c1("Could not allocate I/O buffer!");
         }
      }
   }
   tgs.shared_buffer_stop=
      (tgs.shared_buffer= tgs.shared_buffers[0]) + tgs.shared_buffer_size
   ;
   switch (tgs.mode) {
      case mode_verify:
         /* In verify mode, we start with a "finished" buffer, forcing the
          * next buffer to be read as the first worker thread action. */
         if (tgs.mode != mode_write) {
            tgs.shared_buffer= (void *)tgs.shared_buffer_stop;
         }
         /* Fall through. */
      default: break; /* To avoid switch-case coverage warnings. */
      case mode_compare:
      case mode_diff:
      /* Handle operation modes which do not use multiple threads for
       * simplicity of implementation. */
      slow_comparison();
      goto finished;
   }
   tid= calloc_c5(threads, sizeof *tid);
   tvalid= calloc_c5(threads, sizeof *tvalid);
   {
      static struct cancel_threads_static_resource r;
      r.threads= threads; r.tid= tid; r.tvalid= tvalid;
      r.saved= m.rlist; r.dtor= &cancel_threads_dtor; m.rlist= &r.dtor;
   }
   {
      unsigned i;
      for (i= threads; i--; ) {
         if (
            pthread_create(
                  &tid[i], 0
               ,  tgs.mode == mode_write ? & writer_thread : &reader_thread
               ,  0
            )
         ) {
            error_c1("Could not create worker thread!\n");
         }
         tvalid[i]= 1;
      }
   }
   finished:
   if (fflush(0)) error_c1(msg_write_error);
   cleanup:
   release_c1(&m);
   return m.rollback ? EXIT_FAILURE : EXIT_SUCCESS;
}
