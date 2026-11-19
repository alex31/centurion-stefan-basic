#include "app.h"
#include "work.h"
#ifdef __CPU6_OS6__
int bas_rawkey(void);
#endif
/* The editor keeps only the selected line as text. Edits track the origin
   of each character so committing can preserve untouched numeric tokens. */
static char edit_text[BUFSIZE];
static unsigned edit_len,edit_pos,edit_prefix,edit_dirty;
unsigned char bas_ed_preserve;
static unsigned char edit_capturing;
#define origins bas_work.editor.origin
#define literals bas_work.editor.numbers
#define literal_count bas_work.editor.count
void bas_ed_note(unsigned start,unsigned length,unsigned address)
{
 struct bas_literal *n;
 if(!edit_capturing)return;
 if(!length||start+length>=BUFSIZE||literal_count>=64){ioer=1;return;}
 n=&literals[literal_count++];n->start=(unsigned char)start;
 n->length=(unsigned char)length;n->address=(uint16_t)address;
}
/* Called only during editor retokenization, in the lexer overlay. Unchanged
   numeric text keeps its original token bits, even when LIST rounded the
   display or rendered a non-finite value as '*'. New/changed text is parsed. */
void bas_ed_token(unsigned start,unsigned length)
{
 unsigned i,j,span;struct bas_literal *n;number_t ignored;
 extern mem_t lexliteral;
 if(lexliteral||start>=edit_len||(token!=NUMBER&&token!='-'&&token!='*'))return;
 for(i=0;i<literal_count;i++){
  n=&literals[i];if(origins[start]!=n->start)continue;
  span=length;
  if(token=='-')span=1+parsenumber2(ibuffer+start+2,&ignored);
  if(span!=n->length||start+span>edit_len)continue;
  for(j=0;j<span;j++)if(origins[start+j]!=(unsigned)n->start+j)break;
  if(j!=span)continue;
  token=NUMBER;x=getnumber(n->address,memread2);bi=ibuffer+start+span+1;return;
 }
}
static unsigned digits(unsigned n){unsigned d=1;while(n>=10){n/=10;d++;}return d;}
static unsigned edit_load(unsigned line)
{
 unsigned i;edit_prefix=digits(line)+1;edit_dirty=0;
 ibuffer[0]=ibuffer[1]=0;od=0;ioer=0;literal_count=0;edit_capturing=1;listlines(line,line);edit_capturing=0;
 if(!ibuffer[0]){outnumber(line);outspc();}od=1;
 edit_len=(unsigned char)ibuffer[0];
 for(i=0;i<=edit_len;i++){edit_text[i]=ibuffer[i+1];origins[i]=i<edit_len?(unsigned char)i:255;}
 edit_pos=edit_prefix;
 return !ioer;
}
static void edit_delete(void)
{
 unsigned i;
 for(i=edit_pos;i<edit_len;i++){
  edit_text[i]=edit_text[i+1];
  origins[i]=origins[i+1];
 }
 --edit_len;
 edit_dirty=1;
}
static void edit_insert(char c)
{
 unsigned i;
 if(edit_len>=BUFSIZE-3){outch(7);return;}
 for(i=edit_len+1;i>edit_pos;i--){
  edit_text[i]=edit_text[i-1];
  origins[i]=origins[i-1];
 }
 edit_text[edit_pos]=c;
 origins[edit_pos]=255;
 ++edit_len;
 ++edit_pos;
 edit_dirty=1;
}
/* Keep column zero free for the CRT's backspace echo and enough columns at
   the right to receive both echoed characters of an arrow without wrapping. */
#define EDIT_COLUMNS 76
static void edit_column(unsigned column)
{
 column+=2; /* VT100 columns are one-based, with one margin column. */
 outsc("\033[");
 if(column>=10)outch((char)('0'+column/10));
 outch((char)('0'+column%10));
 outch('G');
}
static void edit_repaint(unsigned first,unsigned start)
{
 unsigned end=edit_len;
 if(end>first+EDIT_COLUMNS)end=first+EDIT_COLUMNS;
 edit_column(start-first);
 outs(edit_text+start,end-start);
 outsc("\033K");
 edit_column(edit_pos-first);
}
static void edit_arrow_echo(unsigned first,unsigned start)
{
 /* CENTOS echoes both printable bytes of the application arrow sequence. */
 unsigned i;
 edit_column(start-first);
 for(i=start;i<start+2;++i)
  outch(i<edit_len&&i<first+EDIT_COLUMNS?edit_text[i]:' ');
 edit_column(edit_pos-first);
}
static unsigned edit_character(void)
{
#ifdef __CPU6_OS6__
 unsigned key=(unsigned)bas_rawkey();
 /* Native function key 4 is Up, while a literal Ctrl-D arrives with bit 7. */
 return key==4?0:key&127;
#else
 unsigned char c=(unsigned char)inch();
 if(stb_done)return 4;
 return c=='\n'?13:c;
#endif
}
static unsigned edit_key(unsigned *escaped)
{
 unsigned key=edit_character();
 if(key!=27&&key!=5)return key;
 key=edit_character();
 if(key!='['&&key!='O')return key;
 *escaped=1;
 do {key=edit_character();} while(key>=32&&key<64);
 if(key=='C')return 6;
 if(key=='D')return 21;
 return key==4?4:0;
}
/* Keep the listing overlay active throughout the keystroke loop. The lexer
   is needed only once, after Enter commits the draft. */
static unsigned edit_input(void)
{
 unsigned first=edit_pos>EDIT_COLUMNS?edit_pos-EDIT_COLUMNS/2:0;
 outsc("\033[?1h");
 edit_repaint(first,first);
 for(;;){
  unsigned key,old_pos=edit_pos,escaped=0,changed=0;
  bas_flush();
  key=edit_key(&escaped);
  if(key==4||key==13){
   outsc("\033[?1l");
   outcr();
   return key==13;
  }
  if(key==21){
   if(edit_pos>edit_prefix)--edit_pos;
  }else if(key==6){
   if(edit_pos<edit_len)++edit_pos;
  }else if(key==2||key==8||key==127){
   if(edit_pos>edit_prefix){
    --edit_pos;
    edit_delete();
    changed=1;
   }
  }else if(key>=32&&key<127){
   edit_insert((char)key);
   changed=1;
  }
  if(edit_pos<first||edit_pos>first+EDIT_COLUMNS){
   first=edit_pos>EDIT_COLUMNS/2?edit_pos-EDIT_COLUMNS/2:0;
   edit_repaint(first,first);
  }else if(changed){
   edit_repaint(first,edit_pos<old_pos?edit_pos:old_pos);
  }else if(escaped){
   edit_arrow_echo(first,old_pos);
  }else if(edit_pos!=old_pos||key==2||key==8||key==127){
   edit_column(edit_pos-first);
  }
 }
}
void bas_editor(unsigned line)
{
 unsigned i;
 if(!line)line=top?bas_line(0):10;
 if(edit_load(line)){
  outsc("EDIT: arrows move, Backspace deletes, Enter saves, Ctrl-D cancels\n");
  while(edit_input()&&edit_dirty){
   ibuffer[0]=(char)edit_len;for(i=0;i<=edit_len;i++)ibuffer[i+1]=edit_text[i];
   bi=ibuffer;st=SINT;nexttoken();
   if(token==NUMBER){ax=(address_t)x;bas_ed_preserve=1;storeline();bas_ed_preserve=0;}else error(EARGS);
   if(!er)break;
   reseterror();
   outsc("Edit not stored: correct the line or free program space\n");
  }
 }else outsc("Line too long to edit safely\n");
 outsc("REPL\n");
}
