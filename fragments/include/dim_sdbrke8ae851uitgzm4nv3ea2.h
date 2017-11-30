/*
 * #include <dim_sdbrke8ae851uitgzm4nv3ea2.h>
 *
 *//** @file
 *
 * Utilities related to the size of an array. *//*
 *
 * (c) 2010-2017 by Guenther Brunthaler.
 * This source file is free software.
 * Distribution is permitted under the terms of the LGPLv3.
 */


#ifndef HEADER_SDBRKE8AE851UITGZM4NV3EA2_INCLUDED
#define HEADER_SDBRKE8AE851UITGZM4NV3EA2_INCLUDED


/**
 * Determine the number of entries in a fixed-size array.
 * @param array A fixed-size @em C array (not just a pointer to it).
 * @return The number of entries in the array; a value of type \c size_t. */
#define DIM(array) (sizeof(array) / sizeof *(array))


#endif /* !HEADER_SDBRKE8AE851UITGZM4NV3EA2_INCLUDED */
