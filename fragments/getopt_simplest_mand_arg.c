#include <getopt_nh7lll77vb62ycgwzwf30zlln.h>
#include <assert.h>

char const *getopt_simplest_mand_arg(
   int *optind_ref, int *optpos_ref, int argc, char **argv
) {
   int i= *optpos_ref, optind= *optind_ref;
   char const *arg;
   assert(optind >= 1);
   assert(optind <= argc);
   if (i == 0 || optind == argc) return 0;
   if (argv[optind][i]) {
      arg= argv[optind++] + i;
      i= 0;
   } else {
      arg= argv[optind++];
   }
   *optind_ref= optind;
   *optpos_ref= i;
   return arg;
}
