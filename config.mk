# quadtree build config options. Set the variables
# as desired.

VERSION = 0.1.1

MAINOBJS = quadtree.o aabb.o

## Uncomment this line to build without thread safety
#NO_THREAD_SAFETY=-DNO_THREAD_SAFETY

## Set BITS to 64 to install to lib64/
BITS=
CC=clang
LINK=cc
OPTIM=-O0
DEBUG=-g -Wall -Wextra
CFLAGS=-std=c99 $(NO_THREAD_SAFETY) $(OPTIM) $(DEBUG)
LFLAGS=$(OPTIM) $(DEBUG)

PREFIX=.
LIBDIR=$(PREFIX)/lib$(BITS)/
INCLUDEDIR=$(PREFIX)/include/
