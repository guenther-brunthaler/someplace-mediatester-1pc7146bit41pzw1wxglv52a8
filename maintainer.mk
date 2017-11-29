# This makefile snippet includes additional rules which are only required by
# the maintainer of the application, and are of no interest to a user who just
# wants to build the application. Thoses rules have been moved here to keep
# the primary Makefile small.

.PHONY: scan depend depend_helper

scan:
	{ \
		t=`printf '\t:'`; t=$${t%?}; \
		printf 'SOURCES = \\\n'; \
		ls *.c | LC_COLLATE=C sort | { \
			while IFS= read -r src; do \
				printf '%s\n' "$$src" >& 8; \
				tgt=$${src%.*}; \
				echo "$$tgt: $$tgt.o "'$$(LIBS)'; \
				echo "$$t"'$$(CC) $$(LDFLAGS) -o $$@' \
					"$$tgt.o "'$$(LIBS)'; \
			done 8>& 1 >& 9; \
		} | sed "s/^/$$t/; "'s/$$/ \\/'; \
		echo; \
	} > sources.mk 9> targets.mk

depend_helper:
	T1=`mktemp $${TMPDIR:-/tmp}/mkdepend.T1_XXXXXXXXXX`; \
	trap 'rm -- "$$T1"' 0; \
	T2=`mktemp $${TMPDIR:-/tmp}/mkdepend.T2_XXXXXXXXXX`; \
	trap 'rm -- "$$T1" "$$T2"' 0; \
	for o in $(OBJECTS); do \
		$(MAKE) CFLAGS="$(AUG_CFLAGS) -MM -MF $$T1" $$o \
		&& cat $$T1 >& 9; \
	done 9> "$$T2"; \
	cat "$$T2" > $(outfile)

depend: clean scan
	outfile=dependencies.mk; > $$outfile; \
	$(MAKE) outfile="$$outfile" depend_helper
