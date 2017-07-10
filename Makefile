LIB = lib$(LIBNAME).a
OBJECTS = $(SOURCES:.c=.o)

LIBNAME = RENAME_ME__name_of_library_wo_lib_prefix
INC_SUBDIR = include

.PHONY: all clean

include sources.mk

all: $(LIB)

clean:
	-rm $(OBJECTS)

COMBINED_CFLAGS= $(CPPFLAGS) $(CFLAGS)
AUG_CFLAGS = $(COMBINED_CFLAGS) -I $(INC_SUBDIR)

.c.o:
	$(CC) $(AUG_CFLAGS) -c $<

$(LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $(OBJECTS)

include dependencies.mk

# Rules not required for just building the application.
include lib_maintainer.mk
