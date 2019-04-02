#include <r4g_internal.h>
#include <r4g/r4g_u0ywydbuiziuzssqsi5l0mdid.h>
#include <stdlib.h>

void prepare_error_w_context_c1(r4g *rc, char const *static_message) {
   if (!rc->errors) {
      rc->static_error_message= static_message;
      rc->errors= 1;
   }
}

void complete_error_w_context_c1(r4g *rc) {
   release_c1(rc);
   exit(EXIT_FAILURE);
}

void error_w_context_c1(r4g *rc, char const *static_message) {
   prepare_error_w_context_c1(rc, static_message);
   complete_error_w_context_c1(rc);
}
