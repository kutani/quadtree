/*
  aabb.h
  2014 JSK (kutani@projectkutani.com)

  Simple axis-aligned bounding box implementation. Part of the Panic
  Panic project.

  Released to the public domain. See LICENSE for details.
*/
#ifndef _AABB_H
 #define _AABB_H

/** \brief axis-aligned bounding box

	Simple struct of four floats, divided into two sub-structs.

	center {x, y} - The center point of the bounding box
	dims {w, h} - The half-width and half-height of the box
*/
typedef struct aabb {
	struct {
		float x;
		float y;
	} center;
	struct {
		float w;
		float h;
	} dims;
} aabb;

/// Malloc's a new aabb struct
/*!
  Mallocs a new aabb struct and sets center and dims to the passed
  x, y, hW, and hH values.
*/
aabb* aabb_new(float x, float y, float hW, float hH);


/// Frees the passed aabb.
void aabb_free(aabb *a);

/// Checks if the point x,y lies within the passed aabb
int aabb_contains(aabb *a, float x, float y);

/// Checks if the two passed aabb's intersect
int aabb_intersects(aabb *a, aabb *b);

#endif
