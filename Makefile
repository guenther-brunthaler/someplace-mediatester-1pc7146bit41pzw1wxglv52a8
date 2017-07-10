LIB = $(LIBDIR)/lib$(LIBDIR).a
OBJECTS = $(SOURCES:.c=.o)
TARGETS = $(OBJECTS:.o=)

LIBDIR =  RENAME_ME__name_of_subdir_containing_a_helper_library
INC_SUBDIR = include

.PHONY: all clean deepclean

include sources.mk

all: $(TARGETS)

clean:
	-rm $(TARGETS) $(OBJECTS)

deepclean: clean
	cd $(LIBDIR) && $(MAKE) clean

COMBINED_CFLAGS= $(CPPFLAGS) $(CFLAGS)
AUG_CFLAGS = $(COMBINED_CFLAGS) -I $(LIBDIR)/$(INC_SUBDIR)

.c.o:
	$(CC) $(AUG_CFLAGS) -c $<

include dependencies.mk
include targets.mk

$(LIB):
	cd $(LIBDIR) && $(MAKE)

include maintainer.mk # Rules not required for just building the application.
