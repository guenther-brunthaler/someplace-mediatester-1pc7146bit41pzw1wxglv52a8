/* #include <r4g/r4g_u0ywydbuiziuzssqsi5l0mdid.h>
 *
 * Resource Control Framework for C, 4th Generation.
 *
 * Version 2019.92
 * Copyright (c) 2016-2019 Guenther Brunthaler. All rights reserved.
 * 
 * This source file is free software.
 * Distribution is permitted under the terms of the LGPLv3. */

#ifndef HEADER_U0YWYDBUIZIUZSSQSI5L0MDID_INCLUDED
#define HEADER_U0YWYDBUIZIUZSSQSI5L0MDID_INCLUDED
#ifdef __cplusplus
   extern "C" {
#endif

/* Resolve dependencies in feature test macros. */
#if defined R4G_EXTENDED_ERROR_SUPPORT && !defined R4G_ENV_TABLE_SUPPORT 
   #define R4G_ENV_TABLE_SUPPORT
#endif

/* This is the primary type alias for working with the struct. No one wants to
 * use its overly long real name! */
typedef struct resource_context_4th_generation r4g;

/* Pointer to a destructor function which will be called for deallocating the
 * associated resource in rc->rlist. The destructor must replace rc->rlist
 * with a pointer to the next resource before actually destroying the current
 * resource, except for the case of a container object which is not yet empty.
 * In the latter case, the destructor should select the oldest element of the
 * container, remove it from the container first, and then deallocate only
 * that element. */
typedef void (*r4g_dtor)(r4g *rc);

/* The central data structure for the R4G framework. One instance exists per
 * application or thread. */
struct resource_context_4th_generation {
   /* The total number of errors which have occurred. A negative number means
    * at least that (negated) number of errors, expressing a saturated count
    * and avoiding that the number of errors shown gets too large. For
    * instance, the value -25 displayed as something like "25 or more errors"
    * makes your program look better than the value 25000 displayed as "25000
    * errors". A very primitive error display routine might choose only to
    * display this information and ignore any error messages that have also
    * been set. An even more primitive routine might just display "an error
    * has occurred" if this is non-zero and ignore anything else. This also
    * acts as the transaction status when resource destructors are called.
    * Zero means that incomplete transactional resources should be committed
    * rather than being rolled back. */
   int errors;
   /* The first error which has occurred. It must be statically allocated.
    * Optionally, a dynamically allocated error message may also have been set
    * which provides a "better" version of the same error message, such as
    * including more detail and follow-up error messages. If an error display
    * routine supports the dynamically allocated error message (which is
    * optional), it should display it instead of this static one. Otherwise,
    * or if there is an error while building the dynamic error message, the
    * static message will act as a fallback. If no error message has been set
    * at all, <errors> can still be reported as unspecific generic errors. */
   char const *static_error_message;
   #ifdef R4G_ENV_TABLE_SUPPORT
      /* Pointer to a dynamically allocated lookup table managed by r4g_put()
       * and r4g_get(). If it is null, r4g_put() will fail. The lookup table
       * can be used to store pointers to arbitrary data, acting as a generic
       * extension mechanism for associating arbitrary data with a resource
       * context beyond the fields already defined here. */
      struct r4g_env *env;
   #endif /* !R4G_ENV_TABLE_SUPPORT */
   /* Null if the resource list is empty, or the address within the last
    * resource list entry where the pointer to its associated destructor
    * function is stored. Destructors need to locate the resource to be
    * destroyed using this address. */
   r4g_dtor *rlist;
};

/* Defines a pointer variable to type resource_t and initializes it with a
 * pointer to the beginning of an object of type resource_t where rc->rlist
 * is a pointer to the struct's component <dtor_member>, which must be a
 * pointer to the destructor function for the object. <var_eq> must be the
 * part of the variable definition after resource_t and before the
 * initialization value, such as "* my_ptr=".
 *
 * Example: R4G_DEFINE_INIT_RPTR(struct my_resource, *r=, rc, dtor);
 *
 * Note that you need to also #include <stddef.h> for actually using this. */
#define R4G_DEFINE_INIT_RPTR(resource_t, var_eq, r4g_rc, dtor_member) \
   resource_t var_eq (void *)( \
      (char *)(r4g_rc)->rlist - offsetof(resource_t, dtor_member) \
   )

/* Returns the current resource context of the executing thread. Depending on
 * the implementation, this may be retrieved from an internal static or
 * thread-local variable. It will abort() in a multithreaded implementation if
 * the thread-local resource context cannot be retrieved. You have to
 * implement this function yourself if it is needed. In the simplest case,
 * just return the address of a static variable of type r4g. */
r4g *r4g_c1(void);

/* Dynamically allocates a memory buffer for a <r4g> and initializes it. Then
 * it stores a pointer to the buffer in a thread-local variable so that
 * r4g_c1() will retrieve it. Returns 0 in case of success of the pointer to a
 * statically allocated error message in case of an error. This should only be
 * used in a multi-threaded application. You have also implent this yourself
 * because it is tightly coupled with the implementation of r4g_c1(). */
char const *new_r4g_thread_context_c0(void);

/* Calls the destructor of the last entry in the specified resource list until
 * there are no more entries left. Destructors need to unlink their entries
 * from the resource list eventually, or this will become an endless loop.
 * Destructors are also free to abort this loop prematurely by performing
 * non-local jumps or calling exit(). Because destructors may refer to objects
 * allocated on the stack somewhere, a longjmp can only be performed safely if
 * the destructors of all resources created since the setjmp() have already
 * been invoked. */
void release_c1(r4g *rc);

/* Like release_c1() but stop releasing when resource <stop_at> would be
 * released next. */
void release_to_c1(r4g *rc, r4g_dtor *stop_at);

/* Raise an error. <static_message> must be a statically allocated error
 * message or null. If no static error message has been set in the resource
 * context <rc>, it will be set to <static_message>. Otherwise it will be
 * ignored because follow-up errors are not supposed to replace the original
 * first error message. If a dynamic error message has been created for <rc>,
 * and if the current build supports dynamic error messages at all (which is
 * optional), <static_message> will also be appended to its current contents,
 * separating both by an empty line unless this would make it the first line
 * of the combined dynamic error message. It is quite common to call
 * error_c1() with a null argument for unlikely errors that will probably
 * never happen. The error display routine might report such errors as
 * "generic", "internal", "unspecified", "anonymous" or something similar.
 * Another reason for a null argument is if the error messages have already
 * been set and should not be touched. In any case, raising an error will
 * increment the current error count, unless it is already saturated. If and
 * when the error count may become saturated is an implementation detail of
 * this function. If an error display routine supports dynamic messages, which
 * is optional, it shall display the dynamic message (unless it is
 * empty/unset) instead of the static message. However, an error display
 * routine might also ignore both kinds of error messages and just display the
 * number of errors which occurred. Or even simpler, it might just report that
 * "an error" has occurred without giving any additional details. It is
 * therfore unwise to only set a dynamic message, because an error display
 * routine might choose to ignore the dynamic message and never display it.
 * Then release_c1() will be called to release all resources, which will also
 * display the error message if an appropriate resource destructor for this
 * purpose has been allocated in the resource list before. Finally, the
 * application will be terminated by exiting with a return code of
 * EXIT_FAILURE. If a different return code is preferred, a resource for this
 * purpose must be allocated, and its destructor should call exit() with the
 * required return code. */
void error_c1(r4g *rc, char const *static_message);

/* Use this to save some typing if you don't already have an <r4g *> around.
 * However, using this means that r4g_c1() needs actually to be implemented. */
#define ERROR_C1(emsg) error_c1(r4g_c1(), emsg)

/* To make your code compile whether or not R4G_ENV_TABLE_SUPPORT has been
 * defined, you should also use #ifdef in your code in order to decide whether
 * or not to provide functions which use this feature. */
#ifdef R4G_ENV_TABLE_SUPPORT
   /* Associate an arbitrary pointer value <data> with a particular binary
    * lookup key in the <rc->env> lookup table. The lookup table must already
    * exist.
    *
    * Note that the lookup table is not a hash table. It is like a hash table
    * without a hash function. This simplifies and speeds up things, but
    * requires the keys to be random as if they were the output of a hash
    * function.
    *
    * <bin_key> should therefore not be text but rather a null-terminated
    * array of bytes taken from /dev/random and put into the source code as a
    * null-terminated "C" string of octal escapes. This will minimize the
    * chance for lookup key collisions, which would be very bad, representing
    * a hard-to-find logic error in your program.
    *
    * The following POSIX shell command will generate and display a proper key
    * as a string literal:
    *
    * $ dd if=/dev/random bs=1 count=16|od -An -vto1|sed 's/ /\\/g;s/.*''/"&"/'
    *
    * This will output something like
    * "\162\041\037\303\112\022\230\210\050\262\236\146\073\332\106\317" which
    * would be a proper binary key argument. */
   void r4g_put_c1(r4g *rc, char const (*bin_key)[17], void *data);

   /* Return the pointer value previously set by r4g_put() and the same key,
    * or return null if no entry with a matching key is present or if no
    * lookup table has been created yet. */
   void *r4g_get_c0(r4g *rc, char const (*bin_key)[17]);

   /* Dynamically allocate a lookup table and set it as <rc->env> which must
    * have been null before. This allows r4g_put_c1() to function. This will
    * also add a destructor to the resource list for deallocating the table
    * and resetting <rc->env back> to null. */
   void create_env_c5(r4g *rc);
#endif /* !R4G_ENV_TABLE_SUPPORT */

/* To make your code compile whether or not R4G_EXTENDED_ERROR_SUPPORT has
 * been defined, you should also use #ifdef in your code in order to decide
 * whether or not to create/update dynamically allocated error messages. */
#ifdef R4G_EXTENDED_ERROR_SUPPORT
   /* Programs which want to support dynamically allocated error messages in
    * addition to statically allocated ones must call this function as soon as
    * possible, because setting dynamic error messages will only be possible
    * afterwards. This function will implicitly call create_env_c5() if
    * <rc->enc> is null. */
   void create_dynamic_error_message_c5(r4g *rc);

   /* Continue doing the remainder what prepare_error_c1() would have done if
    * it had not returned but rather done the same as error_c1(). In other
    * words, calling prepare_error_c1() immediately followed by
    * complete_error_c1() is the same as calling error_c1(). But between both
    * calls, the dynamically allocated error message can be modified and
    * extended, such as by incrementally appending additional optional
    * information to it. */
   void complete_error_c1(r4g *rc);
#endif /* !R4G_EXTENDED_ERROR_SUPPORT */

/* If create_dynamic_error_message_c5() has never been called or if the
 * current build does not support dynamic error messages, just do exactly the
 * same as error_c1(). Otherwise, also do exactly the same as error_c1() would
 * do, but stop at point where error_c1() would call release_c1() and return
 * instead. This allows the caller to append additional information to the
 * current error message or post-process it in some way before actually
 * raising the error. */
void prepare_error_c1(r4g *rc, char const *static_message);

/* Sets the static error message to null and sets the dynamic error message
 * (if it exists and is supported by the current build) to an empty string.
 * Finally, set the current error count to zero. Call this after an error
 * message has been displayed. */
void clear_error_c1(r4g *rc);


#ifdef __cplusplus
   }
#endif
#endif /* !HEADER_U0YWYDBUIZIUZSSQSI5L0MDID_INCLUDED */
