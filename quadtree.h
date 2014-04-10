/*
  quadtree.h
  2014 JSK (kutani@projectkutani.com)

  Part of the Panic Panic project.

  Released to the public domain. See LICENSE for details.
*/
#ifndef _QUADTREE_H
 #define _QUADTREE_H

#ifndef _AABB_H
 #include "aabb.h"
#endif

/// Opaque pointer to a quadtree data structure
typedef struct _qtree* qtree;

/// A function pointer def for determining if an element exists in a range
typedef int (*qtree_fnc)(void *ptr, aabb *range);

/// Create a new qtree
/*!
  Creates a new qtree with a bound of w,h size, centered at x,y.
  
  Uses the passed function pointer fnc to test elements against nodes
  for insertion, and finding.

  Returns a new qtree pointer.
*/
qtree qtree_new(float x, float y, float w, float h, qtree_fnc fnc);

/// Set mutex usage information
/*!
  Sets the mutex-handling functions for the given quadtree
  to enable thread safety.
*/
void qtree_set_mutex(qtree q, void *newfn, void *lockfn, void *unlockfn, void *freefn);

/// Frees the passed qtree
/*!
  Frees a quadtree and all its nodes, but does not touch the data held
  by them.

  If qtree_set_mutex() was used and freefn was not null, will also
  destroy the quadtree's mutex.
*/
void qtree_free(qtree q);

/// Insert an element
/*!
  Inserts the passed element into quadtree q.

  Uses the function passed to qtree_new() to determine where the
  element should go.
*/
void qtree_insert(qtree q, void *ptr);

/// Removes an element from the quadtree
/*!
  Performs a selective removal of the passed element.

  Performs a naive pointer comparison and a depth-first search of the
  tree, so this isn't very fast.
*/
void qtree_remove(qtree q, void *ptr);

/// Set the maximum number of elements per node
/*!
  Sets the maximum elements per quadtree node.

  The default is 4.
*/
void qtree_setMaxNodeCnt(qtree q, uint16_t cnt);

/// Resets a quadtree
/*!
  Clears all nodes held by the quadtree and creates a fresh root node
  with no elements assigned.
*/
void qtree_clear(qtree q);

/// Find all elements within a rectangular bound
/*!
  Performs a search for any elements within the given x,y + w,h
  bound. Returns an array of pointers to any elements (which should be
  freed by the user), and places the number of elements in cnt.
*/
void** qtree_findInArea(qtree q, float x, float y, float w, float h, uint32_t *cnt);

#endif
