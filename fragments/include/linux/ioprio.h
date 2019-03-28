/* Required constants copied from the Linux kernel header file of the same
 * name, because it does not seem to always be shipped as part of the normal
 * Linux kernel developer header files.
 *
 * Should the real header file actually be available and be be included rather
 * than this one, this should be fine and not pose a problem. */

/* SPDX-License-Identifier: GPL-2.0 */

#define IOPRIO_CLASS_SHIFT	(13)
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};
