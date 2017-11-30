#include <pearson.h>
#include <dim_sdbrke8ae851uitgzm4nv3ea2.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#define SWAP(type, v1, v2) { type t= (v1); (v1)= (v2); (v2)= t; }

static uint8_t sbox[1 << 8];

void pearnd_init(void const *key_bytes, size_t count) {
   unsigned i, j, k;
   /* The ARCFOUR sbox has the same structure as the Pearson hash sbox. Use
    * ARCFOUR key setup to set up the Pearson sbox.
    *
    * ARCFOUR operation principle:
    *
    * 1. i= j= 0; s[all indices mod 256], key[all indices mod keylen]
    * 2. Preset s[i]= 0 .. 255
    * 3. (REPEAT) i: always one step to the right
    * 4. j: step by s[i] plus optional key[i] to the right
    * 5. Swap s[] values at i and j.
    * 6. Optionally output s[sum of values just swapped]
    * 7. Discard the first 3072 output values ("ARC4-drop3072") */
   uint8_t const
         *key= key_bytes
      ,  *key_stop= (uint8_t const *)((char const *)key_bytes + count)
   ;
   for (i= (unsigned)DIM(sbox); i--; ) sbox[i]= (uint8_t)i;
   for (i= j= k= 0; i < (unsigned)DIM(sbox); ++i) {
      j= j + sbox[i] + *key & DIM(sbox) - 1;
      if (++key == key_stop) key= key_bytes;
      SWAP(uint8_t, sbox[i], sbox[j]);
   }
   for (i= j= 0, k= 3072; k--; ) {
      j= j + sbox[i] & DIM(sbox) - 1;
      i= i + 1 & DIM(sbox) - 1;
      SWAP(uint8_t, sbox[i], sbox[j]);
   }
}

void pearnd_seek(pearnd_offset *po, uint_fast64_t pos) {
   unsigned i= 0;
   do {
      assert(i < DIM(po->pos));
      po->pos[i++]= pos & UINT8_C(0xff);
   } while (pos>>= 8);
   assert(i <= DIM(po->pos));
   po->limbs= i;
}

void pearnd_generate(void *dst, size_t count, pearnd_offset *po) {
   uint8_t *out= dst;
   uint8_t const *out_stop= (uint8_t const *)((char *)dst + count);
   uint8_t *pos= po->pos, mac;
   unsigned i, limbs= po->limbs, carry;
   assert(limbs >= 1);
   while (out != out_stop) {
      i= 0; mac= UINT8_C(0);
      do mac= sbox[mac ^ pos[i]]; while (++i != limbs);
      *out++= mac;
      for (i= 0; ++pos[i] == 1 << 8; ) {
         pos[i]= 0;
         if (++i == limbs) {
            pos[i]= 0;
            po->limbs= limbs= i + 1;
         }
         assert(i < DIM(po->pos));
      }
   }
}
