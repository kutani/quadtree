#include <stdlib.h>
#include <math.h>

#include "aabb.h"

aabb*
aabb_new(float x, float y, float hW, float hH) {
	aabb* a = malloc(sizeof(aabb));
	a->center.x = x;
	a->center.y = y;
	a->dims.w = hW;
	a->dims.h = hH;
	return a;
}

void
aabb_free(aabb *a) {
	free(a);
}

int
aabb_contains(aabb *a, float x, float y) {
	return (x >= a->center.x-a->dims.w &&
			x <= a->center.x+a->dims.w) &&
		   (y >= a->center.y-a->dims.h &&
			y <= a->center.y+a->dims.h);
}

int
aabb_intersects(aabb *a, aabb *b) {
	return (abs(a->center.x - b->center.x) < (a->dims.w + b->dims.w)) &&
		   (abs(a->center.y - b->center.y) < (a->dims.h + b->dims.h));
}

