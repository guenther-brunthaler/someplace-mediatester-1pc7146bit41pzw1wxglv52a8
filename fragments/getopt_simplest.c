#include <getopt_nh7lll77vb62ycgwzwf30zlln.h>
#include <string.h>
#include <assert.h>

int getopt_simplest(
   int *optind_ref, int *optpos_ref, int argc, char const *const *argv
) {
   int c, i= *optpos_ref, optind= *optind_ref;
   if (argc <= 1) {
      assert(optind == 0);
      assert(i == 0);
      end_of_options:
      c= 0;
      goto done;
   }
   if (optind == 0) optind= 1; /* Start parsing after argv[0]. */
   for (;;) {
      assert(optind < argc);
      assert((size_t)i <= strlen(argv[optind]));
      switch (c= argv[optind][i]) {
         case 0:
            if (i == 0 || ++optind == argc) goto end_of_options;
            i= 0;
            continue;
         case '-':
            if (i == 0 && argv[optind][1] == '\0') goto end_of_options;
            if (i == 1 && argv[optind][i + 1] == '\0') {
               ++optind; goto end_of_options;
            }
            ++i;
            continue;
      }
      break;
   }
   if (i == 0) goto end_of_options;
   ++i;
   done:
   *optind_ref= optind;
   *optpos_ref= i;
   return c;
}
