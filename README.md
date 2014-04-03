quadtree
========

This is a basic C library implementing a general-use quadtree.  It has
optional thread-safety (enabled by passing a mutex and handling function
pointers).

All data structures are opaque to simplify usability; only the
top-level entry functions are available.  However, structure typedefs are
documented and there is nothing stopping you from moving them into the
header files.

## Compiling

quadtree comes with a simple Makefile and a config.mk file for setting
build options. I recommend using these if you're going to build it as
a static library.

If you have clang, you should be able to simply pass `make` to build
the lib; however you may want/need to modify config.mk.

If you want to make use of the Makefile's "install" and "uninstall"
targets, you will probably want to modify the PREFIX variable in
config.mk to point to the install location.

You can pass this to the make command without editing config.mk, like
so:

`PREFIX=/usr/local make -e install`

Or, for 64-bit systems:

`BITS=64 PREFIX=/usr/local make -e install`

I recommend just editing config.mk if you're going to use the
install/uninstall targets, though.

### Doxygen

There is a "docs" Makefile target that will build Doxygen
documentation in html, latex, and man formats, if  your system has
Doxygen installed.

## Usage

Either add quadtree.c and aabb.c to your project, or build them into a
static library (see above), and include quadtree.h and aabb.h where
you want to use them.

See quadtree.h for full API documentation.

Create a new quadtree with `qtree_new()`.

`qtree_new()` takes five arguments: x and y coordinates for the center
point of the quadtree's bound, w and h for the size of the bound, and
a function pointer.  It returns a new qtree pointer.

The function should match the qtree_fnc typedef (in quadtree.h), which
accepts a pointer to some data and a pointer to an aabb struct
describing a boundary.  The function should return 1 if the position of
the data is within the aabb bound.

`qtree_free()` will free all memory held by the quadtree (though not
the stored elements) including, if one has been passed (see below),
the quadtree's mutex.

`qtree_insert()` inserts the passed data pointer into the given
quadtree. It will use the compare function passed to `qtree_new()` to
determine where the element should be placed in the quadtree.

`qtree_remove()` performs a naive depth-first (and not very fast)
pointer comparison between the passed data pointer and all elements
held by the given quadtree. When found, it will remove the element.

`qtree_setMaxNodeCnt()` sets the maximum number of elements any one
node can hold before subdividing that node. The default (defined in
quadtree.c) is 4. If you're planning on a largish number of elements,
you'll probably want to use this function to increase the max node
size so that your stack doesn't get clobbered.

`qtree_clear()` clears all nodes in the passed quadtree,
reinitializing it with an empty root node.

`qtree_findInArea()` is the primary lookup function. It takes a
quadtree pointer, x and y coordinates (describing a corner of the
rectangular bound), width and height values (describing the size of
the bound from x,y), and a pointer to an uint32_t.

It will return an array of pointers to any elements found in the given
boundary, and a count of elements will be stored in *cnt.

The user is expected to free the array of pointers manually, so be
sure to do that.

### Thread Safety

If you want to use the quadtree in a multithreaded environment, you
should call `qtree_set_mutex()`, which takes as its arguments a quadtree
pointer, a pointer to a mutex, and pointers to functions for locking,
unlocking, and freeing the mutex.

If freefn is NULL, `qtree_free()` will not free the mutex data; users
can then manage the memory how they like.

Once mutex data is set, all quadtree operations will be thread safe.

At current there is no parallel node access; only one thread can
access the quadtree at a time. Future feature.

## aabb.c and aabb.h

AABB is "axis-aligned bounding box." The C and header files are purely
utilitarian in this case. I don't package it all into quadtree.c/h
because I make use of aabb's in other places.  Feel free to combine
them all into a single pair of source and header.

The aabb struct is simply four floats packed into a pair of
sub-structs: center {x, y} and dims {w, h}.

aabb.h covers the main documentation, but you'll really only need one
function from aabb.c: `aabb_contains()`.

This function takes a pointer to an aabb struct and x and y
coordinates. It returns 1 if x,y is within the boundary of the passed
aabb, and 0 otherwise.

The function pointer passed to `qtree_new()` (above) will most likely
involve using this function, however feel free to hack it to use
something else.

## Bugs

Possibly. If you discover and/or fix any, please send me a pull
request and I'll take a look.

## License

Public domain. See LICENSE.