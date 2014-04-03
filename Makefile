# This is the raw Makefile for quadtree; you probably
# want config.mk instead.

include config.mk

all: $(MAINOBJS)
	ar rcs libquadtree.a $(MAINOBJS)

install: all
	mkdir -p $(LIBDIR); mkdir -p $(INCLUDEDIR)
	cp libquadtree.a $(LIBDIR)
	cp quadtree.h $(INCLUDEDIR)
	cp aabb.h $(INCLUDEDIR)

uninstall:
	rm $(LIBDIR)/libquadtree.a
	rm $(INCLUDEDIR)/quadtree.h
	rm $(INCLUDEDIR)/aabb.h

clean:
	rm -f $(MAINOBJS)
	rm -f libquadtree.a

docs:
	mkdir -p docs/
	doxygen Doxyfile
