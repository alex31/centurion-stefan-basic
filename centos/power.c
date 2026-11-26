#include "hardware.h"
#include "language.h"
#include "basic.h"
#include <stdint.h>
#include <math.h>
/* Integer powers use binary exponentiation; other finite exponents use the
   existing compact float32 exp/log routines. No double or 64-bit target math. */
float sb_pow(float base,float exponent)
{
 float result=1.0f,factor=base,integer;unsigned negative,odd;
 uint16_t n;
 if(exponent==0.0f)return 1.0f;
 if(base==1.0f)return 1.0f;
 if(!isfinite(base)||!isfinite(exponent))return NAN;
 integer=floorf(exponent);
 if(integer==exponent&&exponent>=-32767.0f&&exponent<=32767.0f){
  negative=exponent<0.0f;n=(uint16_t)(negative?-exponent:exponent);
  /* A positive power may overflow even when its reciprocal is representable. */
  if(negative)factor=1.0f/base;
  while(n){if(n&1U)result*=factor;n>>=1;if(n)factor*=factor;}
  return result;
 }
 if(base==0.0f)return exponent>0.0f?0.0f:INFINITY;
 negative=base<0.0f;
 if(negative&&integer!=exponent)return NAN;
 odd=negative&&(floorf(exponent*0.5f)*2.0f!=exponent);
 if(negative)base=-base;
 result=expf(exponent*logf(base));return odd?-result:result;
}

/* BASIC's integer remainder uses 32-bit operands even though CPU6 int is16.
   Check before conversion and division, including a fractional divisor which
   truncates to zero and the signed minimum / -1 overflow case. */
float sb_mod(float left,float right)
{
 int32_t a,b;
 if(!(left>=-2147483648.0f&&left<2147483648.0f&&
      right>=-2147483648.0f&&right<2147483648.0f)){
  error(EORANGE);return 0.0f;
 }
 a=(int32_t)left;b=(int32_t)right;
 if(!b){error(EDIVIDE);return 0.0f;}
 if(a==(-2147483647L-1L)&&b==-1)return 0.0f;
 return (float)(a%b);
}
