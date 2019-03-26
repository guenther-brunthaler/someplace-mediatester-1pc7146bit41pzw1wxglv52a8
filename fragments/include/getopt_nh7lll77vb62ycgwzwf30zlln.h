/*
 * #include <getopt_nh7lll77vb62ycgwzwf30zlln.h>
 *
 * Option parsing support. Similar to getopt() and friends, but implemented
 * here in local project, and will therefore be always available (getopt is
 * not part of the C standard, although it is part of the POSIX standard). It
 * cannot do everything that getopt() can do, but has a much smaller code
 * footprint and is often still sufficient.
 *
 * Version 2019.83
 *
 * Copyright (c) 2016-2019 Guenther Brunthaler. All rights reserved.
 * 
 * This source file is free software.
 * Distribution is permitted under the terms of the LGPLv3.
 *
 */


#ifndef HEADER_NH7LLL77VB62YCGWZWF30ZLLN_INCLUDED
#define HEADER_NH7LLL77VB62YCGWZWF30ZLLN_INCLUDED
#ifdef __cplusplus
   extern "C" {
#endif


/* Returns the next one-character option encountered by parsing the command
 * line vector <argv> containing <argc> elements (the first of which, if
 * present, is the program name). Returns 0 when no more options have been
 * found. <optind_ref> and <optpos_ref> are pointers to working variables
 * which must have been initialized to 0 by the caller before the first call.
 * The working variable where <optind_ref> points to will contain the argv[]
 * index of the first normal argument when no more options have been found.
 * Option switch clustering and "--" are supported. An argument "-" will not
 * be mis-interpreted as an option. Options will only be recognized before
 * normal arguments! The usage of this function is more portable than
 * getopt(), because the latter function is not part of the C standard. */
int getopt_simplest(
   int *optind_ref, int *optpos_ref, int argc, char **argv
);

/* Consumes a mandatory argument for the last option character parsed. Such an
 * argument "value" for some option "-k" can be specified on the command line
 * in two ways: "-kvalue" or "-k value". The first form is only possible if
 * the argument is not an empty string. Returns a pointer to the argument or
 * null if the argument is missing. */
char const *getopt_simplest_mand_arg(
   int *optind_ref, int *optpos_ref, int argc, char **argv
);

/* Consumes an optional argument for the last option character parsed. Such an
 * argument "value" for some option "-k" can be specified on the command line
 * as: "-kvalue" but not as "-k value". "-k" alone means the optional value
 * has been left out. The optional value cannot be an empty string; this would
 * be interpreted as a missing value. Returns a pointer to the optional
 * argument value or null if it has not been provided. */
char const *getopt_simplest_opt_arg(
   int *optind_ref, int *optpos_ref, int argc, char **argv
);

/* Write an error message complaining about an unsupported option to the
 * standard error stream and ignore any output error. Use this if
 * getopt_simplest() returned an option character which is not supported by
 * the application. */
void getopt_simplest_perror_opt(int bad_option_char);

/* Write an error message complaining about a missing mandatory argument for
 * the encountered option <option_char> to the standard error stream and
 * ignore any output error. Use this if getopt_simplest_mand_arg() returned
 * null. */
void getopt_simplest_perror_missing_arg(int option_char);


#ifdef __cplusplus
   }
#endif
#endif /* !HEADER_NH7LLL77VB62YCGWZWF30ZLLN_INCLUDED */
