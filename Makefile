LIB = lib$(LIBNAME).a
OBJECTS = $(SOURCES:.c=.o)
TARGETS = $(OBJECTS:.o=)

LIBNAME = RENAME_ME__name_of_library_wo_lib_prefix
INC_SUBDIR = include

.PHONY: all clean

include sources.mk

all: $(LIB)

clean:
	-rm $(TARGETS) $(OBJECTS)

COMBINED_CFLAGS= $(CPPFLAGS) $(CFLAGS)
AUG_CFLAGS = $(COMBINED_CFLAGS) -I $(INC_SUBDIR)

.c.o:
	$(CC) $(AUG_CFLAGS) -c $<

$(LIB): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $(OBJECTS)

include dependencies.mk
include targets.mk

include maintainer.mk # Rules not required for just building the application.
