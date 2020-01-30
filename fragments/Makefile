.POSIX:

# Preset portable default build options. Override by either assigning some of
# those directly as part of the "make" command-line arguments. Or export
# environment variables of the same names, plus "export MAKEFLAGS=e" also.
CPPFLAGS = -D NDEBUG
CFLAGS = -O
LDFLAGS = -s

LIB = lib$(LIBNAME).a
OBJECTS = $(SOURCES:.c=.o)

LIBNAME = fragments
INCLUDES = -I . -I include

.PHONY: all clean

include sources.mk

all: $(LIB)

clean:
	-rm $(OBJECTS) $(LIB)

COMBINED_CFLAGS= $(CPPFLAGS) $(CFLAGS)
AUG_CFLAGS = $(COMBINED_CFLAGS) $(INCLUDES)

.c.o:
	$(CC) $(AUG_CFLAGS) -c $<

$(LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $(OBJECTS)

include dependencies.mk

# Rules not required for just building the application.
include lib_maintainer.mk
