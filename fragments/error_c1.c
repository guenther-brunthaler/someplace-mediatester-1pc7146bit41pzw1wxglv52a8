#include <r4g_internal.h>
#include <r4g/r4g_u0ywydbuiziuzssqsi5l0mdid.h>
#include <stdlib.h>

static void impl_complete_error_c1(r4g *rc) {
   release_c1(rc);
   exit(EXIT_FAILURE);
}

void prepare_error_c1(r4g *rc, char const *static_message) {
   if (!rc->errors) {
      rc->static_error_message= static_message;
      rc->errors= 1;
   }
   #ifdef R4G_EXTENDED_ERROR_SUPPORT
      #error NYI
   #else
      impl_complete_error_c1(rc);
   #endif
}

#ifdef R4G_EXTENDED_ERROR_SUPPORT
   void complete_error_c1(r4g *rc) {
      impl_complete_error_c1(rc);
   }
#endif

void error_c1(r4g *rc, char const *static_message) {
   prepare_error_c1(rc, static_message);
   #ifdef R4G_EXTENDED_ERROR_SUPPORT
      complete_error_c1(rc);
   #endif
}
