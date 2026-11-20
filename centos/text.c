#include "app.h"
#include "fileio.h"
#include "decimal.h"
#include "pager.h"
#define LIST_COLUMNS 78 /* CENTOS emits a padding space before CR/LF. */
enum { TEXT_RECORD, LIST_BUFFER, LIST_STREAM };

/* Reconstruct one line. Oversized listings stream through the terminal
   while the same renderer bounds each native SAVE record. */
static unsigned used,failed;
static unsigned char output_mode;
static void put(char c)
{
 if(output_mode!=TEXT_RECORD&&c=='\t')c=' ';
 if(output_mode==LIST_STREAM){
  if(used&&used%LIST_COLUMNS==0)outcr();
  outch(c);
 }else if(used<BAS_TEXT_MAX)bas_record.data[used]=(unsigned char)c;
 else if(output_mode==TEXT_RECORD)failed=1;
 ++used;
}
static void word(const char *s){while(*s)put(*s++);}
static void line_number(unsigned n)
{
 char digits[5];unsigned count=0;
 do {digits[count++]=(char)('0'+n%10);n/=10;} while(n);
 while(count)put(digits[--count]);
}
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
static unsigned render_line(unsigned p,unsigned mode)
{
 unsigned next,i,n,remark=0;char number[20],quote;token_t t,last=EOL;
 used=failed=0;output_mode=mode;
 if(p>=top||mem[p]!=LINENUMBER)return 0;
 line_number(bas_line(p));put(' ');p+=3;
 while(p<top&&mem[p]!=LINENUMBER){
  t=mem[p];next=bas_next(p);if(!next)return 0;
  if(remark){put((char)t);p=next;continue;}
  if(t==NUMBER){
   if(last==NUMBER||last==VARIABLE)put(' ');
   number_t value=getnumber(p+1,memread2);
   if(mode!=TEXT_RECORD&&value>=0.0f&&value<=65535.0f&&value==(unsigned)value){
    line_number((unsigned)value);
   }else{
    if(!bas_ftoa(number,value))return 0;
    if(mode==TEXT_RECORD)shortest(number,value);
    word(number);
   }
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
 if(failed)return 0;
 if(mode==TEXT_RECORD){bas_record.length=used;bas_record.data[used]=0;}
 return p;
}

unsigned bas_render(unsigned p){return render_line(p,TEXT_RECORD);}

static unsigned list_rows(void)
{
 return (used+LIST_COLUMNS-1)/LIST_COLUMNS;
}

/* Reserve one row for keyboard help and wrap before the last column so
   the VT100's automatic wrap setting cannot affect page boundaries. */
static unsigned list_page(unsigned first,unsigned *second)
{
 unsigned p=first,rows=BAS_PAGE_ROWS;
 page_clear();
 while(p<top&&rows){
  unsigned next=render_line(p,LIST_BUFFER),height=list_rows(),offset=0;
  if(!next)return 0;
  if(height>rows){if(p==first)return 0;break;}
  if(used>BAS_TEXT_MAX){
   render_line(p,LIST_STREAM);
   outcr();
  }else{
   while(offset<used){
    unsigned length=used-offset;
    if(length>LIST_COLUMNS)length=LIST_COLUMNS;
    outs((char *)bas_record.data+offset,length);
    outcr();
    offset+=length;
   }
  }
  if(p==first)*second=next;
  rows-=height;
  p=next;
 }
 return p;
}

/* Keep only the offsets that could fit on a previous screen. The source
   stays in the BASIC arena, including when lines wrap across several rows. */
static unsigned list_back(unsigned first,unsigned page)
{
 unsigned recent[BAS_PAGE_ROWS],head=0,count=0,p=0,rows=BAS_PAGE_ROWS;
 while(p<first){
  if(mem[p]==LINENUMBER){
   recent[head++]=p;
   if(head==BAS_PAGE_ROWS)head=0;
   if(count<BAS_PAGE_ROWS)++count;
  }
  p=bas_next(p);
  if(!p)return first;
  byield();
 }
 while(count--){
  unsigned prev,height;
  if(!head)head=BAS_PAGE_ROWS;
  prev=recent[--head];
  if(!page)return prev;
  if(!render_line(prev,LIST_BUFFER))break;
  height=list_rows();
  if(height>rows)break;
  rows-=height;
  first=prev;
 }
 return first;
}

void bas_list(void)
{
 unsigned first=0,second,after,target;
 if(!top)return;
 page_begin();
 while((after=list_page(first,&second))!=0){
  target=first;
  do {
   switch(page_key("LIST")){
   case 'Q':goto done;
   case 'A':target=list_back(first,0);break;
   case 'B':if(after<top)target=second;break;
   case 'C':if(after<top)target=after;break;
   case 'D':target=list_back(first,1);break;
   }
  } while(target==first);
  first=target;
 }
 outsc("\033[24;1H\033[KUnable to list program\n");
done:
 page_end();
}
