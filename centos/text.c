#include "app.h"
#include "fileio.h"
#include "decimal.h"
/* Reconstruct just one line. The shared I/O record is not a source copy. */
static unsigned used,failed;
static void put(char c)
{if(used<BAS_TEXT_MAX)bas_record.data[used++]=(unsigned char)c;else failed=1;}
static void word(const char *s){while(*s)put(*s++);}
/* Remove unnecessary decimal digits while proving each shortening with the
   exact lexer conversion. This keeps common literals such as .1 compact and
   ensures that a line entered at the console still fits a native text record. */
static void shortest(char *s,float expected)
{
 char saved[20];unsigned end,i,j,n;union {float f;uint32_t u;} a,b;
 a.f=expected;if(*s=='-')return;
 for(;;){
  for(end=0;s[end]&&s[end]!='E';end++);
  for(i=0;i<end&&s[i]!='.';i++);
  if(i==end)return;
  for(n=0;(saved[n]=s[n])!=0;n++);
  j=end-1;for(i=j;i<n;i++)s[i]=s[i+1];
  if(s[j-1]=='.'){j--;for(i=j;i<n;i++)s[i]=s[i+1];}
  bas_atof(s,&b.f);if(a.u==b.u)continue;
  i=j;
  while(i){i--;if(s[i]=='.')continue;if(s[i]!='9'){s[i]++;break;}s[i]='0';}
  if(i||s[0]!='0'){bas_atof(s,&b.f);if(a.u==b.u)continue;}
  for(i=0;i<=n;i++)s[i]=saved[i];return;
 }
}
unsigned bas_render(unsigned p)
{
 unsigned next,i,n,remark=0;char number[20],quote;token_t t,last=EOL;
 used=failed=0;
 if(p>=top||mem[p]!=LINENUMBER)return 0;
 n=bas_line(p);writenumber(number,n);word(number);put(' ');p+=3;
 while(p<top&&mem[p]!=LINENUMBER){
  t=mem[p];next=bas_next(p);if(!next)return 0;
  if(remark){put((char)t);p=next;continue;}
  if(t==NUMBER){
   if(last==NUMBER||last==VARIABLE)put(' ');
   if(!bas_ftoa(number,getnumber(p+1,memread2)))return 0;
   shortest(number,getnumber(p+1,memread2));word(number);
  }else if(t==VARIABLE||t==ARRAYVAR||t==STRINGVAR){
   if(last==NUMBER||last==VARIABLE)put(' ');
   put(mem[p+1]);if(mem[p+2])put(mem[p+2]);if(t==STRINGVAR)put('$');
  }else if(t==STRING){
   quote='"';n=(unsigned char)mem[p+1];for(i=0;i<n;i++)if(mem[p+2+i]=='"')quote='\'';
   put(quote);for(i=0;i<n;i++){if(mem[p+2+i]==quote)return 0;put(mem[p+2+i]);}put(quote);
  }else if(t==TREM){word("REM");remark=1;}
  else if(t>=32&&t<=126)put((char)t);
  else{
   for(i=0;gettokenvalue(i)&&gettokenvalue(i)!=t;i++);
   if(!gettokenvalue(i))return 0;
   if(used&&bas_record.data[used-1]!=' ')put(' ');
   word(getkeyword(i));put(' ');
  }
  last=t;p=next;byield();
 }
 bas_record.length=used;bas_record.data[used]=0;return failed?0:p;
}
