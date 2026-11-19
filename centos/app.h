#ifndef STEFAN_CONSOLE_APP_H
#define STEFAN_CONSOLE_APP_H
#include "hardware.h"
#include "language.h"
#include "basic.h"
#include "runtime.h"
extern mem_t mem[],st,er;
extern address_t top,here,ax,himem,memsize;
extern number_t x;
extern token_t token;
extern char ibuffer[],*bi;
extern int stb_done;
extern uint16_t bas_arg;
extern unsigned bas_loaded,bas_new,bas_pending;
#define BAS_NAME_MAX 10
#define BAS_PATTERN_MAX (2 * BAS_NAME_MAX + 1)
extern char bas_slot[11];
extern char bas_name[BAS_PATTERN_MAX + 1],bas_current[11];
enum {BC_NONE,BC_ERROR,BC_RUN,BC_LIST,BC_NEW,BC_EDIT,BC_RENUM,BC_SAVE,BC_LOAD,BC_DIR,BC_BYE,BC_HELP};
void listlines(address_t,address_t);
void resetbasicstate(void);
unsigned bas_parse(const char *);
int bas_command(char *);
unsigned bas_next(unsigned);
int bas_stage_immediate(char *);
unsigned bas_line(unsigned);
unsigned bas_renum(unsigned);
void bas_editor(unsigned);
void bas_files(unsigned);
unsigned bas_query(unsigned);
void bas_publish(void);
unsigned bas_render(unsigned);
extern unsigned bas_staged_line;
int bas_stage(char *);
void bas_copy(unsigned,unsigned);
#endif
