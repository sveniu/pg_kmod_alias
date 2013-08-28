MODULES = kmod_alias

PG_CONFIG = pg_config
PGXS = $(shell $(PG_CONFIG) --pgxs)
INCLUDEDIR = $(shell $(PG_CONFIG) --includedir-server)
include $(PGXS)

kmod_alias.so: kmod_alias.o
	cc -shared -o kmod_alias.so -lkmod kmod_alias.o

kmod_alias.o: kmod_alias.c
	cc -o kmod_alias.o -c kmod_alias.c $(CFLAGS) -I$(INCLUDEDIR)
