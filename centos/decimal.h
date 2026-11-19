#ifndef BASIC_DECIMAL_H
#define BASIC_DECIMAL_H
#include <stdint.h>
/* Positive decimal scanner, as used by the original lexer. A sign is a token. */
unsigned bas_atof(const char *,float *);
/* Round-trip decimal, at most 17 characters plus NUL; 0 means NaN. */
unsigned bas_ftoa(char *,float);
#endif
