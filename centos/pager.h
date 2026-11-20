#ifndef BASIC_PAGER_H
#define BASIC_PAGER_H

#include "app.h"

/* The 24th VT100 row is reserved for keys and then for the REPL prompt. */
#define BAS_PAGE_ROWS 23

static void page_begin(void){outsc("\033[?1h");}
static void page_clear(void){outsc("\033[H\033[J");}
static void page_end(void){outsc("\033[?1l\033[24;1H\033[K");}

static unsigned page_character(void)
{
#ifdef __CPU6_OS6__
 extern int bas_rawkey(void);
 return bas_rawkey()&127;
#else
 unsigned char c=inch();
 return stb_done||c=='\n'?13:c;
#endif
}

static unsigned page_key(const char *title)
{
 unsigned key;
 outsc("\033[24;1H\033[K");
 outsc(title);
 outsc("  Up/Down: line  Left/Right or PgUp/PgDn: page  Q/Enter: exit");
 bas_flush();
 key=page_character();
 if(key==27||key==5){
  unsigned form=page_character();
  if(form=='O'||form=='['){
   key=page_character();
   if(form=='['&&(key=='5'||key=='6')){
    unsigned page=key;
    key=page_character();
    if(key=='~')return page=='5'?'D':'C';
   }
   while(key>=32&&key<64)key=page_character();
   return key>='A'&&key<='D'?key:0;
  }
  key=form;
 }
 return key==13||key=='q'||key=='Q'?'Q':0;
}

#endif
