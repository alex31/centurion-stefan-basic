/* Console-only language configuration, independent of Arduino hardware. */
#define DEBUG 0
#define USELONGJUMP 0
#define BOOLEANMODE -1
#ifndef STEFAN_TINY
#define HASAPPLE1
#endif
#define HASERRORMSG
#define HASTINYBASICINPUT
#if !defined(STEFAN_MINIMAL) && !defined(STEFAN_TINY)
#define HASSTEFANSEXT
#define HASDARTMOUTH
#define HASSTRUCT
#define HASMULTIDIM
#define HASMSSTRINGS
#define HASFULLINSTR
#define HASMULTILINEFUNCTIONS
#define HASLOOPOPT
#define HASLONGNAMES
#endif
#ifdef STEFAN_FLOAT
#define HASFLOAT
#endif

/* Scalar console additions do not pull in the Apple-1 string/array heap. */
#ifdef STEFAN_CENTOS
#define HASCONSOLESCALARS
#define HASPOWER
#define POWERRIGHTTOLEFT
#endif
