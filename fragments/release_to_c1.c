#include <r4g/r4g_u0ywydbuiziuzssqsi5l0mdid.h>

void release_to_c1(r4g *rc, r4g_dtor *stop_at) {
   r4g_dtor *r;
   while ((r= rc->rlist) && r != stop_at) (*r)(rc);
}
