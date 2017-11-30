mediatester: mediatester.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ mediatester.o $(LIBS)
