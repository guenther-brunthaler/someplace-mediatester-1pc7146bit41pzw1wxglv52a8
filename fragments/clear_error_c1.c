#include <r4g/r4g_u0ywydbuiziuzssqsi5l0mdid.h>

void clear_error_c1(r4g *rc) {
   rc->errors= 0;
   rc->static_error_message= 0;
   #ifdef R4G_EXTENDED_ERROR_SUPPORT
      #error NYI
   #endif
}
