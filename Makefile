OBJECTS = $(SOURCES:.c=.o)
TARGETS = $(OBJECTS:.o=)
LIBS = $(LIB_1_SUBDIR)/lib$(LIB_1_SUBDIR).a

LIB_1_SUBDIR =  RENAME_ME__name_of_subdir_containing_a_helper_library
LIB_1_INC_SUBDIR = include

.PHONY: all clean deepclean

include sources.mk

all: $(TARGETS)

clean:
	-rm $(TARGETS) $(OBJECTS)

deepclean: clean
	for lib in $(LIBS); do (cd "`dirname "$$lib"`" && $(MAKE) clean); done

COMBINED_CFLAGS= $(CPPFLAGS) $(CFLAGS)
AUG_CFLAGS = $(COMBINED_CFLAGS) -I $(LIB_1_SUBDIR)/$(LIB_1_INC_SUBDIR)

.c.o:
	$(CC) $(AUG_CFLAGS) -c $<

include dependencies.mk
include targets.mk

$(LIBS):
	for lib in $@; do (cd "`dirname "$$lib"`" && $(MAKE)); done

include maintainer.mk # Rules not required for just building the application.
