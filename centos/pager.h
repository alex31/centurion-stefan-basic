#ifndef BASIC_PAGER_H
#define BASIC_PAGER_H

#include "app.h"

/* The 24th VT100 row is reserved for keys and then for the REPL prompt. */
#define BAS_PAGE_ROWS 23

#ifdef __CPU6_OS6__
#include <os6_curses.h>
static void page_begin(void){bas_flush();os6_curses_keypad(1);}
static void page_clear(void){bas_flush();os6_curses_clear();}
static void page_prompt(void)
{
 bas_flush();
 os6_curses_move(24,1);
 os6_curses_eol();
}
static void page_end(void){bas_flush();os6_curses_keypad(0);page_prompt();}
#else
static void page_begin(void){outsc("\033[?1h");}
static void page_clear(void){outsc("\033[H\033[J");}
static void page_prompt(void){outsc("\033[24;1H\033[K");}
static void page_end(void){outsc("\033[?1l");page_prompt();}

static unsigned page_character(void)
{
 unsigned char c=inch();
 return stb_done||c=='\n'?13:c;
}
#endif

static unsigned page_key(const char *title)
{
 unsigned key;
 page_prompt();
 outsc(title);
 outsc("  Up/Down: line  Left/Right or PgUp/PgDn: page  Q/Enter: exit");
 bas_flush();
#ifdef __CPU6_OS6__
 {
  int input=os6_curses_getch();
  switch(input){
  case OS6_KEY_UP:return 'A';
  case OS6_KEY_DOWN:return 'B';
  case OS6_KEY_RIGHT:case OS6_KEY_PAGE_DOWN:return 'C';
  case OS6_KEY_LEFT:case OS6_KEY_PAGE_UP:return 'D';
  }
  if(input<0)return 'Q';
  key=(unsigned)input;
 }
#else
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
#endif
 return key==13||key=='q'||key=='Q'?'Q':0;
}

#endif
