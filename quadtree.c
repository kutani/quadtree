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

/*!
  Thread safety has a performance overhead penalty, even when not using
  it. Define NO_THREAD_SAFETY to remove all thread safety features at
  build time.
*/
#ifndef NO_THREAD_SAFETY
 #define QTREE_THREADSAFE 1
#else
 #define QTREE_THREADSAFE 0
#endif

#if QTREE_THREADSAFE == 1
 #define QTNEWMUTEX(X) (q->newfn)(X)
 #define QTLOCK(X) (q->lockfn)(X)
 #define QTUNLOCK(X) (q->unlockfn)(X)
 #define QTFREE(X) (q->freefn)(X)
#else
 #define QTNEWMUTEX(X)
 #define QTLOCK(X)
 #define QTUNLOCK(X)
 #define QTFREE(X)
#endif

/// A function pointer def for determining if an element exists in a range
typedef int (*qtree_fnc)(void *ptr, aabb *range);

/// A function pointer def for manipulating mutexes
typedef int (*mutex_fnc)(void *ptr);

typedef void* (*new_mutex_fnc)();

/// A no-op dummy function for non-threadsafe quadtrees
static inline int _lock_dummy() {
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
#if QTREE_THREADSAFE == 1
	void *lock;       ///< Mutex for locking this node
	void *atomlock;   ///< Mutex for atomic wrlockval access
#endif
} qnode;

/// Quadtree container
typedef struct _qtree {
	int8_t wrlockval;    ///< -1 read/insert only; 1 remove only, 0 free
	uint16_t maxnodecap; ///< Maximum element count per node
#if QTREE_THREADSAFE == 1
	void *lock;          ///< Mutex pointer for thread safety
	new_mutex_fnc newfn; ///< Mutex create function pointer
	mutex_fnc lockfn;    ///< Mutex lock function pointer
	mutex_fnc unlockfn;  ///< Mutex unlock function pointer
	mutex_fnc freefn;    ///< Mutex free function pointer
#endif
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
	QTLOCK(q->lock);
	r = q->maxnodecap;
	QTUNLOCK(q->lock);
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

#if QTREE_THREADSAFE == 1
	q->lock = (p->newfn)();
	q->atomlock = (p->newfn)();
#endif
	return q;
}

static void
qnode_free(qtree q, qnode *qn) {
	QTLOCK(qn->lock);

	if(qn->cnt)
		free(qn->elist);

	qn->cnt = 0;

	if(qn->nw) {
		qnode_free(q, qn->nw);
		qnode_free(q, qn->ne);
		qnode_free(q, qn->sw);
		qnode_free(q, qn->se);
	}

	QTUNLOCK(qn->lock);
	QTFREE(qn->lock);
	QTFREE(qn->atomlock);

	free(qn);
}

#if QTREE_THREADSAFE == 1
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
#endif

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

#if QTREE_THREADSAFE == 1
#define ATOM_INCRLOCK(X,Y) _atomic_incrlock(X,Y)
#define ATOM_DECRLOCK(X,Y) _atomic_decrlock(X,Y)
#define ATOM_VAL(X,Y) _atomic_val(X,Y)
#define ATOM_SPIN(W,X,Y,Z) while(_atomic_val(W,X) Y Z) {}
#define ATOM_WRSPIN(X,Y,Z)							\
	for(;;) {										\
		QTLOCK(q->lock);							\
		if(X Y Z) {									\
			QTUNLOCK(q->lock);						\
			break;									\
		}											\
		QTUNLOCK(q->lock);							\
	}												\

/// Inline function for code-cleanliness
static inline void
_atomic_incrlock(qtree q, qnode *qn) {
	QTLOCK(qn->atomlock);
	qn->wrlockval++;
	QTUNLOCK(qn->atomlock);
}

/// Inline function for code-cleanliness
static inline void
_atomic_decrlock(qtree q, qnode *qn) {
	QTLOCK(qn->atomlock);
	qn->wrlockval--;
	QTUNLOCK(qn->atomlock);
}

/// Lookup function for code-cleanliness
static int
_atomic_val(qtree q, qnode *qn) {
	QTLOCK(qn->atomlock);
	int r = qn->wrlockval;
	QTUNLOCK(qn->atomlock);
	return r;
}
#else
#define ATOM_INCRLOCK(X,Y)
#define ATOM_DECRLOCK(X,Y)
#define ATOM_VAL(X,Y)
#define ATOM_SPIN(W,X,Y,Z)
#define ATOM_WRSPIN(X,Y,Z)
#endif

static int
qnode_insert(qtree q, qnode *qn, void *ptr) {
	QTLOCK(qn->lock);

	ATOM_INCRLOCK(q, qn);
	ATOM_SPIN(q, qn, !=, 1);

	QTUNLOCK(qn->lock);
	
	int ret = 0;
	
	if(! (q->cmpfnc)(ptr, &qn->bound))
		goto QN_INS_EXIT;

	if(qn->cnt < qtree_getMaxNodeCnt(q)) {
		add(qn, ptr);
		ret = 1;
		goto QN_INS_EXIT;
	}

	if(! qn->nw)
		subdivide(q, qn);


	ATOM_DECRLOCK(q, qn);

	if(qnode_insert(q,qn->nw,ptr))
		return 1;
	else if(qnode_insert(q,qn->ne,ptr))
		return 1;
	else if(qnode_insert(q,qn->sw,ptr))
		return 1;
	else if(qnode_insert(q,qn->se,ptr))
		return 1;

	return ret;

	QN_INS_EXIT:
	ATOM_DECRLOCK(q, qn);
	return ret;
}

static void* 
qnode_remove(qtree q, qnode *qn, void *ptr) {
	QTLOCK(qn->lock);

	ATOM_INCRLOCK(q, qn);
	ATOM_SPIN(q, qn, !=, 1);

	QTUNLOCK(qn->lock);
    
	if(qn->cnt) {
		for(uint16_t i=0; i<qn->cnt; i++) {
			if(qn->elist[i] == ptr) {
				drop(qn, i);
				ptr = NULL;
				goto QN_REM_EXIT;
			}
		}
	}

	ATOM_DECRLOCK(q, qn);
	
	if(! qn->nw)
		return NULL;

	if(qnode_remove(q, qn->nw, ptr)) return ptr;
	if(qnode_remove(q, qn->ne, ptr)) return ptr;
	if(qnode_remove(q, qn->sw, ptr)) return ptr;
	if(qnode_remove(q, qn->se, ptr)) return ptr;

	return NULL;

	QN_REM_EXIT:
	ATOM_DECRLOCK(q, qn);
return ptr;
}

static void
qnode_getInRange(qtree q, qnode *qn, retlist *r) {
	QTLOCK(qn->lock);

	ATOM_DECRLOCK(q, qn);
	ATOM_SPIN(q, qn, >=, 0);

	QTUNLOCK(qn->lock);
    
	if(qn->cnt) {
		if(! aabb_intersects(&qn->bound, &r->range))
			goto QN_GET_EXIT;

		for(uint16_t i=0; i<qn->cnt; i++)
			if((q->cmpfnc)(qn->elist[i], &r->range))
				retlist_add(r, qn->elist[i]);
	}

	if(! qn->nw)
		goto QN_GET_EXIT;

	ATOM_INCRLOCK(q, qn);

	qnode_getInRange(q, qn->nw, r);
	qnode_getInRange(q, qn->ne, r);
	qnode_getInRange(q, qn->sw, r);
	qnode_getInRange(q, qn->se, r);

	return;

	QN_GET_EXIT:
	ATOM_INCRLOCK(q, qn);
}

/* exports */

qtree
qtree_new(float x, float y, float w, float h, qtree_fnc fnc) {
	qtree q = malloc(sizeof(_qtree));
	memset(q, 0, sizeof(_qtree));

#if QTREE_THREADSAFE == 1
	q->newfn = (void *)_lock_dummy;
	q->lockfn = _lock_dummy;
	q->unlockfn = _lock_dummy;
	q->freefn = _lock_dummy;
#endif
	
	q->maxnodecap = QTREE_STDCAP;
	q->cmpfnc = fnc;
	q->root = qnode_new(q, x+(w/2),y+(h/2),w/2,h/2);

#if QTREE_THREADSAFE == 1
	q->root->lock = NULL;
	q->root->atomlock = NULL;
#endif
	return q;
}

void
qtree_set_mutex(qtree q, new_mutex_fnc newfn, mutex_fnc lockfn,
				mutex_fnc unlockfn, mutex_fnc freefn) {
#if QTREE_THREADSAFE == 1
	q->newfn = newfn;
	q->lockfn = lockfn;
	q->unlockfn = unlockfn;
	q->freefn = freefn;

	q->lock = (newfn)();

	qnode_set_lock(q, q->root);
#endif
}

void
qtree_free(qtree q) {
	void *m;
	mutex_fnc u, f;

	QTLOCK(q->lock);
#if QTREE_THREADSAFE == 1
	m = q->lock;
	u = q->unlockfn;
	f = q->freefn;
#endif
	
	if(q->root) qnode_free(q, q->root);
	
	memset(q, 0, sizeof(_qtree));

#if QTREE_THREADSAFE == 1
	(u)(m);
	(f)(m);
#endif
	
	free(q);
}

void
qtree_insert(qtree q, void *ptr) {
#if QTREE_THREADSAFE == 1
	QTLOCK(q->lock);
	q->wrlockval--;
	QTUNLOCK(q->lock);

	ATOM_WRSPIN(q->wrlockval, <, 0);
#endif
    
	qnode_insert(q, q->root, ptr);

#if QTREE_THREADSAFE == 1
	QTLOCK(q->lock);
	q->wrlockval++;
	QTUNLOCK(q->lock);
#endif
}

void
qtree_remove(qtree q, void *ptr) {
#if QTREE_THREADSAFE == 1
	QTLOCK(q->lock);
	q->wrlockval--;
	QTUNLOCK(q->lock);

	ATOM_WRSPIN(q->wrlockval, <, 0);
#endif
	
	qnode_remove(q, q->root, ptr);

#if QTREE_THREADSAFE == 1
	QTLOCK(q->lock);
	q->wrlockval++;
	QTUNLOCK(q->lock);
#endif
}

void
qtree_setMaxNodeCnt(qtree q, uint16_t cnt) {
	QTLOCK(q->lock);
	q->maxnodecap = cnt || 1;
	QTUNLOCK(q->lock);
}

void
qtree_clear(qtree q) {
#if QTREE_THREADSAFE == 1
	QTLOCK(q->lock);
	q->wrlockval++;
	QTUNLOCK(q->lock);

	ATOM_WRSPIN(q->wrlockval, ==, 1);
#endif

	float x = q->root->bound.center.x;
	float y = q->root->bound.center.y;
	float w = q->root->bound.dims.w;
	float h = q->root->bound.dims.h;
	qnode *qn = q->root;
	
	q->root = qnode_new(q, x, y, w, h);

#if QTREE_THREADSAFE == 1
	QTLOCK(q->lock);
	q->wrlockval--;
	QTUNLOCK(q->lock);
#endif

	qnode_free(q, qn);
}

void**
qtree_findInArea(qtree q, float x, float y, float w, float h, uint32_t *cnt) {
#if QTREE_THREADSAFE == 1
	QTLOCK(q->lock);
	q->wrlockval--;
	QTUNLOCK(q->lock);

	ATOM_WRSPIN(q->wrlockval, <, 0);
#endif

	float hw = w/2;
	float hh = h/2;

	retlist ret;
	memset(&ret, 0, sizeof(retlist));

	ret.range.center.x = x+hw;
	ret.range.center.y = y+hh;
	ret.range.dims.w = hw;
	ret.range.dims.h = hh;

	qnode_getInRange(q, q->root, &ret);

#if QTREE_THREADSAFE == 1
	QTLOCK(q->lock);
	q->wrlockval++;
	QTUNLOCK(q->lock);
#endif

	*cnt = ret.cnt;
	return ret.list;
}
