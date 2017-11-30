#include <dim_sdbrke8ae851uitgzm4nv3ea2.h>
#include <pearson.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#define APPROXIMATE_BUFFER_SIZE (16ul << 20)

static int write_mode;
static uint8_t *shared_buffer;
static uint8_t const *shared_buffer_stop;
static size_t blksz;
uint_fast64_t pos;
              
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
   if (argc < 4 || argc > 5) {
      bad_arguments:
      error=
         "Arguments: (write | verify) <password> <io_block_size>"
         " [ <starting_byte_offset> ]"
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
   blksz= (size_t)atou64(&error, argv[3]); if (error) goto fail;
   {
      size_t mask= 512;
      while (blksz ^ mask) {
         size_t nmask;
         if ((nmask= mask + mask) < mask) {
            error= "I/O block size must be a power of 2 and be >= 512!";
            goto fail;
         }
         mask= nmask;
      }
   }
   if (argc == 5) {
      pos= atou64(&error, argv[4]); if (error) goto fail;
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
   ++threads; /* Create one thread more than processors just for safety. */
   shared_buffer_size= (APPROXIMATE_BUFFER_SIZE + blksz - 1) / blksz * blksz;
   if (
      (
         shared_buffer= mmap(
               0, shared_buffer_size, PROT_READ | PROT_WRITE
            ,  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
         )
      ) == MAP_FAILED
   ) {
      error= "Could not allocate I/O buffer!";
      goto fail;
   }
   shared_buffer_stop= shared_buffer + shared_buffer_size;
   pearnd_seek(&po, pos);
   if (!write_mode) {
      error= "Verify mode is not yet implemented!";
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
   if (shared_buffer) {
      void *old= shared_buffer; shared_buffer= 0;
      if (munmap(old, shared_buffer_size)) {
         exotic_error:
         error= "Internal error (this should normally never happen).\n";
         goto fail;
      }
   }
   return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
