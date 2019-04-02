#include <r4g/r4g_u0ywydbuiziuzssqsi5l0mdid.h>

void release_c1(r4g *rc) {
   while (rc->rlist) (*rc->rlist)(rc);
}
