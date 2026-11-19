#ifndef CENTURION_STEFAN_HARDWARE_H
#define CENTURION_STEFAN_HARDWARE_H
#include <stdint.h>
#include <stdio.h>
#ifndef __SDCC
#include <stdlib.h>
#else
int abs(int value);
#endif
#include <math.h>
#include <float.h>
float sb_pow(float x,float y);
float sb_mod(float x,float y);
typedef unsigned char byte;
#ifdef __SDCC
/* Unused radio-driver declarations in runtime.h require this type. No such
   driver or 64-bit operation is included in the Centurion language profile. */
typedef unsigned long long uint64_t;
#endif
#define ARDUINO 1 /* Embedded setup()/loop() entry points; no POSIX main. */
#define PROGMEM
#ifndef MEMSIZE
#define MEMSIZE 4096
#endif
#ifndef BUFSIZE
#define BUFSIZE 128
#endif
#ifndef STACKSIZE
#define STACKSIZE 24
#endif
#ifndef GOSUBDEPTH
#define GOSUBDEPTH 8
#endif
#ifndef FORDEPTH
#define FORDEPTH 8
#endif
#define LINECACHESIZE 8
#define FNLIMIT 4
#define SPIRAMSBSIZE 16
#define BREAKCHAR 3
#define TERMINATEAFTERRUN 0
void bas_flush(void);
void bas_emit(char);
unsigned long millis(void);
#endif
