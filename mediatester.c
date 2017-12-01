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

static int write_mode;
static uint8_t *shared_buffer, *shared_buffers[2];
static uint8_t const *shared_buffer_stop;
static size_t blksz= 4096;
static size_t work_segments= 64;
static size_t work_segment_sz;
static uint_fast64_t pos;
static unsigned active_prng_threads;
static pthread_mutex_t workers_mutex= PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t io_mutex= PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workers_wakeup_call= PTHREAD_COND_INITIALIZER;

#define CEIL_DIV(num, den) (((num) + (den) - 1) / (den))

static void *thread_func(void *dummy) {
   char const *error;
   struct {
      unsigned wmtx: 1;
   } have= {0};
   (void)dummy;
   if (pthread_mutex_lock(&workers_mutex)) {
      error= "Could not lock mutex!";
      fail:
      (void)fprintf(stderr, "Failure in thread: %s\n", error);
      goto cleanup;
   }
   have.wmtx= 1;
   if (pthread_cond_wait(&workers_wakeup_call, &workers_mutex)) {
      error= "Could not wait for condition variable!";
      goto fail;
   }
   cleanup:
   if (have.wmtx) {
      have.wmtx= 0;
      if (pthread_mutex_unlock(&workers_mutex)) {
         error= "Could not unlock mutex!";
         goto fail;
      }
   }
   return 0;
}
              
static uint_fast64_t atou64(char const **error, char const *numeric) {
   uint_fast64_t v= 0, nv;
   unsigned digit, i;
   for (i= 0; ; ++i) {
      switch (numeric[i]) {
         default:
            *error= "Invalid decimal digit!";
            fail:
            return 0;
         case '\0':
            if (i) return v;
            *error= "Decimal number without any digits!";
            goto fail;
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
      if ((nv= v * 10 + digit) < v) {
         *error= "Decimal number is too large!";
         goto fail;
      }
      v= nv;
   }
}

int main(int argc, char **argv) {
   char const *error= 0;
   pearnd_offset po;
   size_t shared_buffer_size;
   unsigned threads;
   pthread_t *tid= 0;
   char *tvalid= 0;
   if (argc < 3 || argc > 4) {
      bad_arguments:
      error=
         "Arguments: (write | verify) <password> [ <starting_byte_offset> ]"
      ;
      fail:
      (void)fprintf(
            stderr, "%s failed: %s\n"
         ,  argc ? argv[0] : "<unnamed program>", error
      );
      goto cleanup;
   }
   if (!strcmp(argv[1], "write")) write_mode= 1;
   else if (!strcmp(argv[1], "verify")) write_mode= 0;
   else goto bad_arguments;
   if (!*argv[2]) {
      error= "Key must not be empty!";
      goto fail;
   }
   pearnd_init(argv[2], strlen(argv[2]));
   if (argc == 4) {
      pos= atou64(&error, argv[3]); if (error) goto fail;
      if (pos % blksz) {
         error= "Starting offset must be a multiple of the I/O block size!";
         goto fail;
      }
      if ((off_t)pos < 0) {
         error= "Numeric overflow in offset!";
         goto fail;
      }
      if (lseek(write_mode ? 1 : 0, (off_t)pos, SEEK_SET) == (off_t)-1) {
         error= "Could not reposition standard stream to starting position!";
         goto fail;
      }
   } else {
      pos= 0;
   }
   {
      long rc;
      if (
            (rc= sysconf(_SC_NPROCESSORS_ONLN)) == -1
         || (threads= (unsigned)rc , (long)threads != rc)
      ) {
         error= "Could not determine number of available CPU processors!";
         goto fail;
      }
   }
   if (threads < work_segments) {
      if (threads == 1) work_segments= 1;
      work_segments= work_segments / threads * threads;
      assert(work_segments >= 1);
   } else {
      work_segments= threads;
   }
   work_segment_sz= CEIL_DIV(APPROXIMATE_BUFFER_SIZE, work_segments);
   work_segment_sz= CEIL_DIV(work_segment_sz, blksz) * blksz;
   shared_buffer_size= work_segment_sz * work_segments;
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
         ,  pos
         ,  (unsigned)blksz
         ,  threads
         ,  (unsigned long)work_segment_sz
         ,  (unsigned long)work_segments
         ,  (unsigned long)shared_buffer_size
         ,  (unsigned)DIM(shared_buffers)
      ) <= 0
   ) {
      goto write_error;
   }
   /* The threads will encrypt/decrypt. An additional thread does I/O. The
    * main program thread only waits for termination of the other threads. */
   ++threads;
   {
      unsigned i;
      for (i= (unsigned)DIM(shared_buffers); i--; ) {
         if (
            (
               shared_buffers[i]= mmap(
                     0, shared_buffer_size, PROT_READ | PROT_WRITE
                  ,  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
               )
            ) == MAP_FAILED
         ) {
            error= "Could not allocate I/O buffer!";
            goto fail;
         }
      }
   }
   shared_buffer_stop= shared_buffer= shared_buffers[0] + shared_buffer_size;
   pearnd_seek(&po, pos);
   if (!write_mode) {
      error= "Verify mode is not yet implemented!";
      goto fail;
   }
   if (!(tvalid= calloc(threads, sizeof *tvalid))) {
      malloc_error:
      error= "Memory allocation failure!";
      goto fail;
   }
   if (!(tid= calloc(threads, sizeof *tid))) goto malloc_error;
   {
      unsigned i;
      for (i= threads; i--; ) {
         if (pthread_create(&tid[i], 0, &thread_func, 0)) {
            error= "Could not create worker thread!\n";
            goto fail;
         }
         tvalid[i]= 1;
      }
   }
   if (pthread_cond_broadcast(&workers_wakeup_call)) {
      error= "Could not wake up worker threads!";
      goto fail;
   }
   for (;;) {
      ssize_t written;
      uint8_t const *out;
      size_t left;
      pearnd_generate(shared_buffer, left= shared_buffer_size, &po);
      out= shared_buffer;
      if ((written= write(1, out, left)) <= 0) {
         if (written == 0) break;
         if (written != -1) goto exotic_error;
         if (errno != EINTR) goto write_error;
         written= 0;
      }
      if ((size_t)written > left) goto exotic_error;
      out+= (size_t)written;
      left-= (size_t)written;
   }
   if (fflush(0)) {
      write_error:
      error= "write error!";
      goto fail;
   }
   cleanup:
   if (tvalid) {
      unsigned i;
      for (i= threads; i--; ) {
         if (tvalid[i]) {
            tvalid[i]= 0;
            if (pthread_join(tid[i], 0)) {
               error= "Failure waiting for child thread to terminate!";
               goto fail;
            }
         }
      }
      free(tid);
      free(tvalid); tvalid= 0;
   }
   {
      unsigned i;
      for (i= (unsigned)DIM(shared_buffers); i--; ) {
         if (shared_buffers[i]) {
            void *old= shared_buffers[i]; shared_buffers[i]= 0;
            if (munmap(old, shared_buffer_size)) {
               exotic_error:
               error= "Internal error (this should normally never happen).\n";
               goto fail;
            }
         }
      }
   }
   return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
