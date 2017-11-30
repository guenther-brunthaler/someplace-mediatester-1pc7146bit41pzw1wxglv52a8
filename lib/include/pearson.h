#ifndef HEADER_ESM240BGRPZJZAJ61R9RMUV23_INCLUDED
#define HEADER_ESM240BGRPZJZAJ61R9RMUV23_INCLUDED

/* Pseudo-random generation based on Pearson's idea for hashing.
 *
 * The idea is to first initialize a SBOX with an arbitrary permutation of all
 * possible byte values.
 *
 * Then create a PRNG stream by hashing the current stream position as a
 * little endian base-256 number with as few bytes as possible. Which means
 * small offsets will hash faster.
 *
 * The SBOX will be kept in a global variable for simpicity; it does not need
 * to be modified after it has been initialized.
 */

#include <stdint.h>
#include <stdlib.h>

typedef struct {
   uint8_t pos[8];
   unsigned limbs;
} pearnd_offset;

/* Select PRNG sequence based on binary key. Only one instance of a sequence
 * for the whole application is supported. */
void pearnd_init(void const *key_bytes, size_t count);

/* Set absolute starting position. */
void pearnd_seek(pearnd_offset *po, uint_fast64_t pos);

/* Fill buffer with the next <count> PRNG bytes, starting at the current
 * stream position. */
void pearnd_generate(void *dst, size_t count, pearnd_offset *po);

#endif /* !HEADER_ESM240BGRPZJZAJ61R9RMUV23_INCLUDED */
