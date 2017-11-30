#include <dim_sdbrke8ae851uitgzm4nv3ea2.h>
#include <pearson.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
   size_t bytes;
   if (argc != 4) {
      error= "Arguments: password, length, offset";
      fail:
      (void)fprintf(stderr, "%s failed: %s\n", error);
      goto cleanup;
   }
   pearnd_init(argv[1], strlen(argv[1]));
   {
      uint_fast64_t pos= atou64(&error, argv[3]); if (error) goto fail;
      pearnd_seek(&po, bytes);
   }
   bytes= (size_t)atou64(&error, argv[2]); if (error) goto fail;
   while (bytes--) {
      uint8_t buf;
      pearnd_generate(&buf, sizeof buf, &po);
      if (putchar(buf) != buf) {
         write_error:
         error= "write error!";
         goto fail;
      }
   }
   if (fflush(0)) goto write_error;
   cleanup:
   return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
