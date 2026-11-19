#include "app.h"
/* Walk the original short-token format, checking every payload boundary. */
unsigned bas_next(unsigned p)
{
 token_t t;unsigned size=1;
 if(p>=top)return 0;t=mem[p];
 if(t==LINENUMBER||t==VARIABLE||t==ARRAYVAR||t==STRINGVAR)size=3;
 else if(t==NUMBER)size=1+sizeof(number_t);
 else if(t==STRING){if(p+1>=top)return 0;size=2+(unsigned char)mem[p+1];}
 if(size>top-p)return 0;return p+size;
}
/* The line number of a token header and the ordinal of an existing line. */
unsigned bas_line(unsigned p){return getaddress((address_t)(p+1),memread2);}
static unsigned bas_ordinal(unsigned line)
{
 unsigned p=0,n=0,next;
 while(p<top){if(mem[p]==LINENUMBER){n++;if(bas_line(p)==line)return n;}
  next=bas_next(p);if(!next)return 0;p=next;byield();
 }return 0;
}

unsigned bas_renum(unsigned interval)
{
 unsigned p=0,next,count=0,previous=0,pass,target,ordinal,after;
 unsigned remark=0;number_t value;token_t t;
 if(!interval){puts("RENUM interval must be 1..65535");return 0;}
 while(p<top){
  next=bas_next(p);if(!next)goto malformed;
  if(mem[p]==LINENUMBER){target=bas_line(p);if(target<=previous)goto malformed;previous=target;count++;}
  else if(!count)goto malformed;
  next=bas_next(p);if(!next)goto malformed;p=next;
 }
 if(count>65535U/interval){puts("RENUM line number overflow");return 0;}
 /* First validate every target. Then update references while old headers
    still exist. Finally change headers. No renumbering table or source copy. */
 for(pass=0;pass<2;pass++){
  p=0;remark=0;
  while(p<top){
   t=mem[p];next=bas_next(p);
   if(t==LINENUMBER)remark=0;
   else if(t==TREM)remark=1;
   else if(!remark&&(t==TGOTO||t==TGOSUB||t==TTHEN)){
    if(next>=top||mem[next]!=NUMBER){if(t==TTHEN)goto skip;goto computed;}
    after=bas_next(next);
    if(after<top&&mem[after]!=LINENUMBER&&mem[after]!=':'&&mem[after]!=EOL)goto computed;
    value=getnumber(next+1,memread2);
    if(!(value>=1&&value<=65535.0f))goto missing;
    target=(unsigned)value;if((number_t)target!=value)goto computed;
    ordinal=bas_ordinal(target);if(!ordinal)goto missing;
    if(pass)setnumber(next+1,memwrite2,(number_t)(ordinal*interval));
   }
skip: p=next;byield();
  }
 }
 p=0;count=0;
 while(p<top){if(mem[p]==LINENUMBER)setaddress(p+1,memwrite2,(++count)*interval);p=bas_next(p);byield();}
 return 1;
computed:puts("RENUM refused: computed branch target");return 0;
missing:puts("RENUM refused: target line does not exist");return 0;
malformed:puts("Invalid token program");return 0;
}
