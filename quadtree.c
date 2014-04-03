/*
  quadtree.c
  2014 JSK (kutani@projectkutani.com)

  Part of the Panic Panic project.

  Released to the public domain. See LICENSE for details.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "aabb.h"

/// Default node size cap
#define QTREE_STDCAP 4

/// A function pointer def for determining if an element exists in a range
typedef int (*qtree_fnc)(void *ptr, aabb *range);

/// A function pointer def for manipulating mutexes
typedef void (*mutex_fnc)(void *ptr);

/// A no-op dummy function for non-threadsafe quadtrees
static void
_lock_dummy() {
	// NOP
}

/// Quadtree node
typedef struct qnode {
	uint16_t cnt;     ///< Number of elements in this node
	aabb bound;       ///< Area this node covers
	void **elist;     ///< List of element pointers
	struct qnode *nw; ///< NW quadrant of this node
	struct qnode *ne; ///< NE quadrant of this node
	struct qnode *sw; ///< SW quadrant of this node
	struct qnode *se; ///< SE quadrant of this node
} qnode;

/// Quadtree container
typedef struct _qtree {
	uint16_t maxnodecap; ///< Maximum element count per node
	void *lock;          ///< Mutex pointer for thread safety
	mutex_fnc lockfn;    ///< Mutex lock function pointer
	mutex_fnc unlockfn;  ///< Mutex unlock function pointer
	mutex_fnc freefn;    ///< Mutex free function pointer
	qnode *root;         ///< Root node
	qtree_fnc cmpfnc;    ///< Element range compare function pointer
} _qtree;

typedef struct _qtree* qtree;

/// Simple container for returning found elements
typedef struct retlist {
	uint32_t cnt; ///< Number of elements found
	aabb range;   ///< Range to use for searching
	void **list;  ///< Array of pointers to found elements
} retlist;

static void
retlist_add(retlist *r, void *p) {
	r->list = realloc(r->list, sizeof(void*)*(r->cnt+1));
	r->list[r->cnt] = p;
	r->cnt++;
}

static qnode*
qnode_new(float x, float y, float hW, float hH) {
	qnode *q = malloc(sizeof(qnode));
	memset(q, 0, sizeof(qnode));
	q->bound.center.x = x;
	q->bound.center.y = y;
	q->bound.dims.w = hW;
	q->bound.dims.h = hH;
	return q;
}

static void
qnode_free(qnode *q) {
	if(q->cnt)
		free(q->elist);

	if(q->nw) {
		qnode_free(q->nw);
		qnode_free(q->ne);
		qnode_free(q->sw);
		qnode_free(q->se);
	}

	free(q);
}

static void
add(qnode *q, void *p) {
	q->elist = realloc(q->elist, sizeof(void*)*(q->cnt+1));
	q->elist[q->cnt] = p;
	q->cnt++;
}

static void
drop(qnode *q, uint16_t idx) {
	void **narry = malloc(sizeof(void*)*(q->cnt-1));
	memcpy(narry, q->elist, idx);
	memcpy(narry[idx], q->elist[idx+1], q->cnt-idx-1);
	void **old = q->elist;
	q->elist = narry;
	free(old);
}

static void
subdivide(qnode *q) {
	float cx = q->bound.center.x;
	float cy = q->bound.center.y;
	float hw = q->bound.dims.w/2;
	float hh = q->bound.dims.h/2;

	q->nw = qnode_new(cx-hw, cy-hh, hw, hh);
	q->ne = qnode_new(cx+hw, cy-hh, hw, hh);
	q->sw = qnode_new(cx-hw, cy+hh, hw, hh);
	q->se = qnode_new(cx+hw, cy+hh, hw, hh);
}

static int
qnode_insert(qtree p, qnode *q, void *ptr) {
	if(! (p->cmpfnc)(ptr, &q->bound)) return 0;

	if(q->cnt < p->maxnodecap) {
		add(q, ptr);
		return 1;
	}

	if(! q->nw)
		subdivide(q);

	if(qnode_insert(p,q->nw,ptr))
		return 1;
	else if(qnode_insert(p,q->ne,ptr))
		return 1;
	else if(qnode_insert(p,q->sw,ptr))
		return 1;
	else if(qnode_insert(p,q->se,ptr))
		return 1;

	return 0;
}

static void* 
qnode_remove(qnode *q, void *ptr) {
	if(! q->elist)
		return NULL;

	for(uint16_t i=0; i<q->cnt; i++) {
		if(q->elist[i] == ptr) {
			drop(q, i);
			return ptr;
		}
	}

	if(! q->nw) return NULL;

	if(qnode_remove(q->nw, ptr)) return ptr;
	if(qnode_remove(q->ne, ptr)) return ptr;
	if(qnode_remove(q->sw, ptr)) return ptr;
	if(qnode_remove(q->se, ptr)) return ptr;

	return NULL;
}

static void
qnode_getInRange(qtree p, qnode *q, retlist *r) {
	if(! q->elist)
		return;

	if(! aabb_intersects(&q->bound, &r->range))
		return;

	for(uint16_t i=0; i<q->cnt; i++)
		if((p->cmpfnc)(q->elist[i], &r->range))
			retlist_add(r, q->elist[i]);

	if(! q->nw)
		return;

	qnode_getInRange(p, q->nw, r);
	qnode_getInRange(p, q->ne, r);
	qnode_getInRange(p, q->sw, r);
	qnode_getInRange(p, q->se, r);
}

/* exports */

qtree
qtree_new(float x, float y, float w, float h, qtree_fnc fnc) {
	qtree q = malloc(sizeof(_qtree));
	memset(q, 0, sizeof(_qtree));

	q->lockfn = _lock_dummy;
	q->unlockfn = _lock_dummy;
	q->freefn = _lock_dummy;
	
	q->maxnodecap = QTREE_STDCAP;
	q->cmpfnc = fnc;
	q->root = qnode_new(x+(w/2),y+(h/2),w/2,h/2);
	return q;
}

void
qtree_set_mutex(qtree q, void *mutex, mutex_fnc lockfn,
				mutex_fnc unlockfn, mutex_fnc freefn) {
	q->lock = mutex;
	q->lockfn = lockfn;
	q->unlockfn = unlockfn;
	q->freefn = freefn;
}

void
qtree_free(qtree q) {
	void *m;
	mutex_fnc u, f;
	
	(q->lockfn)(q->lock);
	m = q->lock;
	u = q->unlockfn;
	f = q->freefn;
	
	if(q->root) qnode_free(q->root);
	
	memset(q, 0, sizeof(_qtree));
	
	(u)(m);
	
	if(f)
		(f)(m);
	
	free(q);
}

void
qtree_insert(qtree q, void *ptr) {
	(q->lockfn)(q->lock);
	qnode_insert(q, q->root, ptr);
	(q->unlockfn)(q->lock);
}

void
qtree_remove(qtree q, void *ptr) {
	(q->lockfn)(q->lock);
	qnode_remove(q->root, ptr);
	(q->unlockfn)(q->lock);
}

void
qtree_setMaxNodeCnt(qtree q, uint16_t cnt) {
	(q->lockfn)(q->lock);
	q->maxnodecap = cnt;
	(q->unlockfn)(q->lock);
}

void
qtree_clear(qtree q) {
	(q->lockfn)(q->lock);

	float x = q->root->bound.center.x;
	float y = q->root->bound.center.y;
	float w = q->root->bound.dims.w;
	float h = q->root->bound.dims.h;	
	qnode_free(q->root);
	q->root = qnode_new(x, y, w, h);

	(q->unlockfn)(q->lock);
}

void**
qtree_findInArea(qtree q, float x, float y, float w, float h, uint32_t *cnt) {
	(q->lockfn)(q->lock);

	float hw = w/2;
	float hh = h/2;

	retlist ret;
	memset(&ret, 0, sizeof(retlist));

	ret.range.center.x = x+hw;
	ret.range.center.y = y+hh;
	ret.range.dims.w = hw;
	ret.range.dims.h = hh;

	qnode_getInRange(q, q->root, &ret);

	(q->unlockfn)(q->lock);

	*cnt = ret.cnt;
	return ret.list;
}
