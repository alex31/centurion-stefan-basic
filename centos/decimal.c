#include "decimal.h"
/* Exact integer conversion for binary32, independent of host double/libc.
   120 significant decimal digits exceed the longest binary32 midpoint (113).
   A discarded nonzero tail is therefore needed only to break an exact tie.
   64 base-256 digits cover the significand and powers of five. */
#define BIG 64
typedef struct {unsigned n;unsigned char d[BIG];} integer;
static void init(integer *a,uint32_t n)
{a->n=0;do{a->d[a->n++]=(unsigned char)n;n>>=8;}while(n);}
static void trim(integer *a){while(a->n>1&&!a->d[a->n-1])a->n--;}
static void mul(integer *a,unsigned m,unsigned carry)
{unsigned i,v;for(i=0;i<a->n;i++){v=a->d[i]*m+carry;a->d[i]=(unsigned char)v;carry=v>>8;}if(carry)a->d[a->n++]=(unsigned char)carry;}
static unsigned divide(integer *a,unsigned m)
{unsigned i=a->n,r=0,v;while(i){i--;v=(r<<8)|a->d[i];a->d[i]=(unsigned char)(v/m);r=v%m;}trim(a);return r;}
static int compare(const integer *a,const integer *b)
{unsigned i;if(a->n!=b->n)return a->n>b->n?1:-1;i=a->n;while(i){i--;if(a->d[i]!=b->d[i])return a->d[i]>b->d[i]?1:-1;}return 0;}
static void subtract(integer *a,const integer *b)
{unsigned i,borrow=0,v;for(i=0;i<a->n;i++){v=(unsigned)a->d[i]-(i<b->n?b->d[i]:0)-borrow;a->d[i]=(unsigned char)v;borrow=(v>>8)&1;}trim(a);}
static int bits(const integer *a)
{unsigned v=a->d[a->n-1];int n=(a->n-1)*8;while(v){n++;v>>=1;}return n;}
static void shift(integer *a,int n){while(n-->0)mul(a,2,0);}
static unsigned exact_decimal(const char *s,float *result)
{
 const char *start=s,*mark;integer a,b;union {uint32_t u;float f;} value;
 unsigned digits=0,fraction=0,negative=0,sticky=0,d;int scale=0,exponent=0,e,n,cmp;
 uint32_t m=0;
 init(&a,0);
 while((*s>='0'&&*s<='9')||(*s=='.'&&!fraction)){
  if(*s=='.'){fraction=1;s++;continue;}
  d=*s++-'0';
  if(digits||d){if(digits<120){mul(&a,10,d);digits++;}else{scale++;if(d)sticky=1;}}
  if(fraction)scale--;
 }
 if(*s=='e'||*s=='E'){
  mark=s++;if(*s=='-'||*s=='+'){negative=*s=='-';s++;}
  if(*s<'0'||*s>'9')s=mark;
  else{while(*s>='0'&&*s<='9'){if(exponent<1000)exponent=exponent*10+(*s-'0');s++;}scale+=negative?-exponent:exponent;}
 }
 value.u=0;
 if(!digits||scale+(int)digits < -45)goto done;
 if(scale+(int)digits>39){value.u=0x7f800000UL;goto done;}
 init(&b,1);n=scale;
 while(n>0){mul(&a,5,0);n--;}
 while(n<0){mul(&b,5,0);n++;}
 e=bits(&a)-bits(&b);
 if(e>=0)shift(&b,e);else shift(&a,-e);
 if(compare(&a,&b)<0){mul(&a,2,0);e--;}
 e+=scale;
 if(e>127){value.u=0x7f800000UL;goto done;}
 if(e < -150)goto done;
 n=e < -126 ? 24+e+126 : 24;
 if(!n){if(compare(&a,&b)>0||sticky)value.u=1;goto done;}
 while(n--){m<<=1;if(compare(&a,&b)>=0){subtract(&a,&b);m++;}mul(&a,2,0);}
 cmp=compare(&a,&b);if(cmp>0||(cmp==0&&(sticky||(m&1))))m++;
 if(e < -126)value.u=m;
 else{
  if(m==0x1000000UL){m>>=1;e++;}
  value.u=e>127?0x7f800000UL:((uint32_t)(e+127)<<23)|(m&0x7fffffUL);
 }
done:*result=value.f;return (unsigned)(s-start);
}

unsigned bas_atof(const char *s,float *result)
{
 const char *p=s;
 unsigned value=0;
 while(*p>='0'&&*p<='9'){
  unsigned digit=(unsigned)(*p-'0');
  if(value>6553u||(value==6553u&&digit>5))break;
  value=value*10u+digit;
  ++p;
 }
 /* Every 16-bit integer is exactly representable in binary32. The decimal
    scanner still handles fractions, exponents and larger significands. */
 if(p!=s&&(*p<'0'||*p>'9')&&*p!='.'&&*p!='E'&&*p!='e'){
  *result=(float)value;
  return (unsigned)(p-s);
 }
 return exact_decimal(s,result);
}
unsigned bas_ftoa(char *out,float input)
{
 union {uint32_t u;float f;} v;integer a;char digits[116];
 uint32_t m;unsigned sign,n=0,i,j,used,round=0;int e,scale=0,power;
 v.f=input;sign=(unsigned)(v.u>>31);v.u&=0x7fffffffUL;
 if(v.u>0x7f800000UL)return 0;
 used=0;if(sign)out[used++]='-';
 if(v.u==0x7f800000UL){out[used++]='1';out[used++]='E';out[used++]='9';out[used++]='9';out[used++]='9';goto done;}
 if(!v.u){out[used++]='0';goto done;}
 e=(int)(v.u>>23);m=v.u&0x7fffffUL;
 if(e){m|=0x800000UL;e-=150;}else e=-149;
 init(&a,m);
 if(e>=0)shift(&a,e);else{scale=e;while(e++<0)mul(&a,5,0);}
 do{digits[n++]=(char)('0'+divide(&a,10));}while(a.n>1||a.d[0]);
 for(i=0,j=n-1;i<j;i++,j--){char c=digits[i];digits[i]=digits[j];digits[j]=c;}
 power=(int)n+scale-1;
 if(n>9){
  round=digits[9]>'5';
  if(digits[9]=='5'){round=digits[8]&1;for(i=10;i<n;i++)if(digits[i]!='0')round=1;}
  n=9;
  if(round){i=n;while(i&&digits[i-1]=='9')digits[--i]='0';if(i)digits[i-1]++;else{digits[0]='1';power++;}}
 }
 while(n>1&&digits[n-1]=='0')n--;
 if(power>=0&&power<=6){
  for(i=0;i<=(unsigned)power;i++)out[used++]=i<n?digits[i]:'0';
  if(i<n){out[used++]='.';while(i<n)out[used++]=digits[i++];}
 }else if(power<0&&power>=-3){
  out[used++]='0';out[used++]='.';for(e=-1;e>power;e--)out[used++]='0';
  for(i=0;i<n;i++)out[used++]=digits[i];
 }else{
  out[used++]=digits[0];if(n>1){out[used++]='.';for(i=1;i<n;i++)out[used++]=digits[i];}
  out[used++]='E';if(power<0){out[used++]='-';power=-power;}
  if(power>=10)out[used++]=(char)('0'+power/10);out[used++]=(char)('0'+power%10);
 }
done:out[used]=0;return used;
}
