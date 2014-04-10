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

#include <SDL2/SDL.h>

#include "aabb.h"

/// Default node size cap
#define QTREE_STDCAP 4

/// A function pointer def for determining if an element exists in a range
typedef int (*qtree_fnc)(void *ptr, aabb *range);

/// A function pointer def for manipulating mutexes
typedef int (*mutex_fnc)(void *ptr);

typedef void* (*new_mutex_fnc)();

/// A no-op dummy function for non-threadsafe quadtrees
static int _lock_dummy() {
	return 0;
}

/// Quadtree node
typedef struct qnode {
	int8_t wrlockval; ///< -1 read-only; 1 write-only; 0 free
	uint16_t cnt;     ///< Number of elements in this node
	aabb bound;       ///< Area this node covers
	void **elist;     ///< List of element pointers
	struct qnode *nw; ///< NW quadrant of this node
	struct qnode *ne; ///< NE quadrant of this node
	struct qnode *sw; ///< SW quadrant of this node
	struct qnode *se; ///< SE quadrant of this node
	void *lock;       ///< Mutex for locking this node
	void *atomlock;   ///< Mutex for atomic wrlockval access
} qnode;

/// Quadtree container
typedef struct _qtree {
	int8_t wrlockval;    ///< -1 read/insert only; 1 remove only, 0 free
	uint16_t maxnodecap; ///< Maximum element count per node
	void *lock;          ///< Mutex pointer for thread safety
	new_mutex_fnc newfn; ///< Mutex create function pointer
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

static uint16_t
qtree_getMaxNodeCnt(qtree q) {
	uint16_t r;
	(q->lockfn)(q->lock);
	r = q->maxnodecap;
	(q->unlockfn)(q->lock);
	return r;
}

static qnode*
qnode_new(qtree p, float x, float y, float hW, float hH) {
	qnode *q = malloc(sizeof(qnode));
	memset(q, 0, sizeof(qnode));
	q->bound.center.x = x;
	q->bound.center.y = y;
	q->bound.dims.w = hW;
	q->bound.dims.h = hH;

	q->lock = (p->newfn)();
	q->atomlock = (p->newfn)();
	return q;
}

static void
qnode_free(qtree p, qnode *q) {
	(p->lockfn)(q->lock);

	if(q->cnt)
		free(q->elist);

	q->cnt = 0;

	if(q->nw) {
		qnode_free(p, q->nw);
		qnode_free(p, q->ne);
		qnode_free(p, q->sw);
		qnode_free(p, q->se);
	}

	(p->unlockfn)(q->lock);
	(p->freefn)(q->lock);
	(p->freefn)(q->atomlock);

	free(q);
}

static void
qnode_set_lock(qtree p, qnode *q) {
	if(q->lock)
		(p->freefn)(q->lock);
	if(q->atomlock)
		(p->freefn)(q->atomlock);

	q->lock = (p->newfn)();
	q->atomlock = (p->newfn)();

	if(q->nw) {
		qnode_set_lock(p, q->nw);
		qnode_set_lock(p, q->ne);
		qnode_set_lock(p, q->sw);
		qnode_set_lock(p, q->se);
	}
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
	
	// This is a little (lot) ugly; a pair of memcpy's would be
	// better, but I had some problems with it
	for(uint16_t i=0,skip=0; i<q->cnt; i++) {
		if(i == idx) { skip++; continue; }
		narry[i-skip] = q->elist[i];
	}
	
	void **old = q->elist;
	q->elist = narry;
	free(old);
	q->cnt--;
}

static void
subdivide(qtree p, qnode *q) {
	float cx = q->bound.center.x;
	float cy = q->bound.center.y;
	float hw = q->bound.dims.w/2;
	float hh = q->bound.dims.h/2;

	q->nw = qnode_new(p, cx-hw, cy-hh, hw, hh);
	q->ne = qnode_new(p, cx+hw, cy-hh, hw, hh);
	q->sw = qnode_new(p, cx-hw, cy+hh, hw, hh);
	q->se = qnode_new(p, cx+hw, cy+hh, hw, hh);
}

/// Inline function for code-cleanliness
static inline void
atomic_incrlock(qtree p, qnode *q) {
	(p->lockfn)(q->atomlock);
	q->wrlockval++;
	(p->unlockfn)(q->atomlock);
}

/// Inline function for code-cleanliness
static inline void
atomic_decrlock(qtree p, qnode *q) {
	(p->lockfn)(q->atomlock);
	q->wrlockval--;
	(p->unlockfn)(q->atomlock);
}

/// Lookup function for code-cleanliness
static int
atomic_val(qtree p, qnode *q) {
	(p->lockfn)(q->atomlock);
	int r = q->wrlockval;
	(p->unlockfn)(q->atomlock);
	return r;
}

static int
qnode_insert(qtree p, qnode *q, void *ptr) {
	(p->lockfn)(q->lock);
	
    atomic_incrlock(p, q);
    
	while(atomic_val(p,q) != 1) {}

	(p->unlockfn)(q->lock);
	

	int ret = 0;
	
	if(! (p->cmpfnc)(ptr, &q->bound))
		goto QN_INS_EXIT;

	if(q->cnt < qtree_getMaxNodeCnt(p)) {
		add(q, ptr);
		ret = 1;
		goto QN_INS_EXIT;
	}

	if(! q->nw)
		subdivide(p, q);

    atomic_decrlock(p, q);

	if(qnode_insert(p,q->nw,ptr))
		return 1;
	else if(qnode_insert(p,q->ne,ptr))
		return 1;
	else if(qnode_insert(p,q->sw,ptr))
		return 1;
	else if(qnode_insert(p,q->se,ptr))
		return 1;

	return ret;

	QN_INS_EXIT:
	atomic_decrlock(p, q);
	return ret;
}

static void* 
qnode_remove(qtree p, qnode *q, void *ptr) {
	(p->lockfn)(q->lock);

	atomic_incrlock(p, q);

	while(atomic_val(p,q) != 1) {}

	(p->unlockfn)(q->lock);
	

	if(q->cnt) {
		for(uint16_t i=0; i<q->cnt; i++) {
			if(q->elist[i] == ptr) {
				drop(q, i);
				ptr = NULL;
				goto QN_REM_EXIT;
			}
		}
	}
    
	atomic_decrlock(p, q);
	
	if(! q->nw)
		return NULL;

	if(qnode_remove(p, q->nw, ptr)) return ptr;
	if(qnode_remove(p, q->ne, ptr)) return ptr;
	if(qnode_remove(p, q->sw, ptr)) return ptr;
	if(qnode_remove(p, q->se, ptr)) return ptr;

	return NULL;

	QN_REM_EXIT:
	atomic_decrlock(p, q);
	return ptr;
}

static void
qnode_getInRange(qtree p, qnode *q, retlist *r) {
	(p->lockfn)(q->lock);

	atomic_decrlock(p, q);
	
	while(atomic_val(p, q) >= 0) {}

	(p->unlockfn)(q->lock);
	
	
	if(q->cnt) {
		if(! aabb_intersects(&q->bound, &r->range))
			goto QN_GET_EXIT;

		for(uint16_t i=0; i<q->cnt; i++)
			if((p->cmpfnc)(q->elist[i], &r->range))
				retlist_add(r, q->elist[i]);
	}

	if(! q->nw)
		goto QN_GET_EXIT;

	atomic_incrlock(p, q);

	qnode_getInRange(p, q->nw, r);
	qnode_getInRange(p, q->ne, r);
	qnode_getInRange(p, q->sw, r);
	qnode_getInRange(p, q->se, r);

	return;

	QN_GET_EXIT:
	atomic_incrlock(p, q);
}

/* exports */

qtree
qtree_new(float x, float y, float w, float h, qtree_fnc fnc) {
	qtree q = malloc(sizeof(_qtree));
	memset(q, 0, sizeof(_qtree));

	q->newfn = (void *)_lock_dummy;
	q->lockfn = _lock_dummy;
	q->unlockfn = _lock_dummy;
	q->freefn = _lock_dummy;
	
	q->maxnodecap = QTREE_STDCAP;
	q->cmpfnc = fnc;
	q->root = qnode_new(q, x+(w/2),y+(h/2),w/2,h/2);
	q->root->lock = NULL;
	q->root->atomlock = NULL;
	return q;
}

void
qtree_set_mutex(qtree q, new_mutex_fnc newfn, mutex_fnc lockfn,
				mutex_fnc unlockfn, mutex_fnc freefn) {
	q->newfn = newfn;
	q->lockfn = lockfn;
	q->unlockfn = unlockfn;
	q->freefn = freefn;

	q->lock = (newfn)();

	qnode_set_lock(q, q->root);
}

void
qtree_free(qtree q) {
	void *m;
	mutex_fnc u, f;
	
	(q->lockfn)(q->lock);
	m = q->lock;
	u = q->unlockfn;
	f = q->freefn;
	
	if(q->root) qnode_free(q, q->root);
	
	memset(q, 0, sizeof(_qtree));
	
	(u)(m);
    
	(f)(m);
	
	free(q);
}

void
qtree_insert(qtree q, void *ptr) {
	(q->lockfn)(q->lock);
	q->wrlockval--;
	(q->unlockfn)(q->lock);
    
	for(;;) {
		(q->lockfn)(q->lock);
		if(q->wrlockval < 0) {
			(q->unlockfn)(q->lock);
			break;
		}
		(q->unlockfn)(q->lock);
	}
    
	qnode_insert(q, q->root, ptr);

	(q->lockfn)(q->lock);
	q->wrlockval++;
	(q->unlockfn)(q->lock);
}

void
qtree_remove(qtree q, void *ptr) {
	(q->lockfn)(q->lock);
	q->wrlockval--;
	(q->unlockfn)(q->lock);
    
	for(;;) {
		(q->lockfn)(q->lock);
		if(q->wrlockval < 0) {
			(q->unlockfn)(q->lock);
			break;
		}
		(q->unlockfn)(q->lock);
	}
	
	qnode_remove(q, q->root, ptr);

	(q->lockfn)(q->lock);
	q->wrlockval++;
	(q->unlockfn)(q->lock);
}

void
qtree_setMaxNodeCnt(qtree q, uint16_t cnt) {
	(q->lockfn)(q->lock);
	q->maxnodecap = cnt || 1;
	(q->unlockfn)(q->lock);
}

void
qtree_clear(qtree q) {
	(q->lockfn)(q->lock);
	q->wrlockval++;
	(q->unlockfn)(q->lock);
    
	for(;;) {
		(q->lockfn)(q->lock);
		if(q->wrlockval == 1) {
			(q->unlockfn)(q->lock);
			break;
		}
		(q->unlockfn)(q->lock);
	}

	float x = q->root->bound.center.x;
	float y = q->root->bound.center.y;
	float w = q->root->bound.dims.w;
	float h = q->root->bound.dims.h;
	qnode *qn = q->root;
	
	q->root = qnode_new(q, x, y, w, h);

	(q->lockfn)(q->lock);
	q->wrlockval--;
	(q->unlockfn)(q->lock);

	qnode_free(q, qn);
}

void**
qtree_findInArea(qtree q, float x, float y, float w, float h, uint32_t *cnt) {
	(q->lockfn)(q->lock);
	q->wrlockval--;
	(q->unlockfn)(q->lock);

	for(;;) {
		(q->lockfn)(q->lock);
		if(q->wrlockval < 0) {
			(q->unlockfn)(q->lock);
			break;
		}
		(q->unlockfn)(q->lock);
	}

	float hw = w/2;
	float hh = h/2;

	retlist ret;
	memset(&ret, 0, sizeof(retlist));

	ret.range.center.x = x+hw;
	ret.range.center.y = y+hh;
	ret.range.dims.w = hw;
	ret.range.dims.h = hh;

	qnode_getInRange(q, q->root, &ret);

	(q->lockfn)(q->lock);
	q->wrlockval++;
	(q->unlockfn)(q->lock);

	*cnt = ret.cnt;
	return ret.list;
}
