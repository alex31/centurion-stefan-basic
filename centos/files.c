#include "app.h"
#include "fileio.h"
#include "pager.h"
/* Native type A source files. The fixed wrappers restore the caller
   after text, numeric and lexer operations in the shared overlay slot. */
unsigned bas_loaded,bas_new,bas_pending;
char bas_slot[11];
static void current_name(void){unsigned i;for(i=0;i<11;i++){bas_current[i]=bas_name[i];if(!bas_name[i])break;}}
static uint16_t crc_byte(uint16_t crc,unsigned byte)
{
 static const uint16_t nibble[] = {
  0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
  0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef
 };
 crc=(uint16_t)(crc<<4)^nibble[(crc>>12)^(byte>>4)];
 return (uint16_t)(crc<<4)^nibble[(crc>>12)^(byte&15)];
}
struct source_check {
 unsigned length;
 uint16_t crc;
};

enum { CHECK_SOURCE, LOAD_SOURCE, CHECK_NUMBERED_TEXT };

/* CENTOS uses type A for both BASIC text and JCL jobs. Identify numbered
   source without touching the lexer or the program in memory. */
static int numbered_text(void)
{
 unsigned previous=0;
 int status;
 while((status=bas_io_read())==0){
  const unsigned char *text=bas_record.data;
  unsigned line=0;
  while(*text==' '||*text=='\t')++text;
  if(!*text)continue;
  while(*text>='0'&&*text<='9'){
   if(line>6553U||(line==6553U&&*text>'5'))goto invalid;
   line=line*10U+(*text++-'0');
  }
  if(line<=previous)break;
  for(;*text;++text)if((*text<32&&*text!=9)||*text>126)goto invalid;
  previous=line;
  byield();
 }
 return !bas_io_close()&&status==1&&previous!=0;
invalid:
 bas_io_close();
 return 0;
}

/* The caller has opened the input. Check each original record before the
   lexer normalizes keyword case in its buffer, and always close the file. */
static int read_and_close(unsigned apply,struct source_check *check)
{
 unsigned length=0,last=0;
 uint16_t crc=65535u;
 int status;
 while((status=bas_io_read())==0){
  for(unsigned i=0;i<bas_record.length;++i){
   unsigned c=bas_record.data[i];
   if((c<32&&c!=9)||c>126)goto close_file;
   crc=crc_byte(crc,c);
  }
  crc=crc_byte(crc,10);
  int n=bas_stage((char *)bas_record.data);
  if(n<0||er)break;
  if(!n)continue;
  if(bas_staged_line<=last||(unsigned)n>MEMSIZE-1-length)break;
  last=bas_staged_line;
  if(apply)bas_copy(length,(unsigned)n);
  length+=(unsigned)n;
  byield();
 }
close_file:
 check->length=length;
 check->crc=crc;
 return !bas_io_close() && status==1;
}

/* The input overlay owns both passes. A failed validation or second open
   leaves the current program intact. Once copying has begun, only publish
   the token length after the source and its final close have been checked. */
int bas_read_source(const char *name,unsigned action)
{
 struct source_check original,copied;
 if(bas_io_open(name,1))return 0;
 if(action==CHECK_NUMBERED_TEXT)return numbered_text();
 if(!read_and_close(0,&original))return 0;
 if(action==CHECK_SOURCE)return 1;
 if(bas_io_open(name,1))return 0;
 if(!read_and_close(1,&copied)||original.length!=copied.length||original.crc!=copied.crc){
  top=0;
  bas_loaded=2;
  puts("Load I/O changed during copy; program cleared");
  return 0;
 }
 top=copied.length;
 bas_loaded=1;
 return 1;
}
static int render_program(unsigned write)
{
 unsigned p=0,next;
 while(p<top){next=bas_render(p);if(!next)return 0;
  if(write&&bas_io_write())return 0;p=next;byield();
 }return 1;
}
static int name_matches(const unsigned char *entry,const char *pattern)
{
 unsigned length=BAS_NAME_MAX,index=0,retry=0;
 const char *after_star=0;

 while(length&&(entry[length-1]&127)==' ')--length;
 while(index<length){
  if(*pattern=='*'){
   after_star=++pattern;
   retry=index;
  }else if(*pattern&&(entry[index]&127)==(unsigned char)*pattern){
   ++pattern;
   ++index;
  }else if(after_star){
   /* Retry the suffix after letting the last star consume one more byte. */
   pattern=after_star;
   index=++retry;
  }else{
   return 0;
  }
 }
 while(*pattern=='*')++pattern;
 return !*pattern;
}
/* BFREEdddd names are installer-owned staging files. The reserve can be
   extended with ordinary CENTOS .NEW operations; it is not a hardware limit. */
static int staging_name(const unsigned char *entry)
{
 unsigned i;
 for(i=0;i<5;i++)if((entry[i]&127)!=(unsigned char)"BFREE"[i])return 0;
 for(i=5;i<9;i++)if((entry[i]&127)<'0'||(entry[i]&127)>'9')return 0;
 return (entry[9]&127)==' ';
}
static int pick_staging(void)
{
 unsigned char *entry;unsigned i;int found=0;
 if(bas_dir_open())return 0;
 while((entry=bas_dir_next())!=0){
  if((entry[13]&15)==2&&staging_name(entry)){
   for(i=0;i<9;i++)bas_slot[i]=(char)(entry[i]&127);bas_slot[9]=0;found=1;break;
  }
 }
 if(bas_dir_close())found=0;return found;
}
static int file_kind(const char *name)
{
 unsigned char *entry;int type=-1;
 if(bas_dir_open())return -2;
 while((entry=bas_dir_next())!=0)if(name_matches(entry,name)){type=entry[13]&15;break;}
 if(bas_dir_close())return -2;return type;
}
typedef unsigned char dir_name_t[BAS_NAME_MAX];

static void copy_dir_name(dir_name_t to,const unsigned char *from)
{
 unsigned i;
 for(i=0;i<BAS_NAME_MAX;i++)to[i]=from[i];
}

static void decode_dir_name(char *name,const dir_name_t entry)
{
 unsigned i;
 for(i=0;i<BAS_NAME_MAX&&(entry[i]&127)!=' ';i++)name[i]=entry[i]&127;
 name[i]=0;
}

static int name_order(const unsigned char *left,const unsigned char *right,int forward)
{
 unsigned i;
 for(i=0;i<BAS_NAME_MAX;i++){
  int difference=(left[i]&127)-(right[i]&127);
  if(difference)return forward?difference:-difference;
 }
 return 0;
}

/* Collect at most a screen of the closest names in either direction. Close
   the directory before reusing its I/O workspace for the source files. */
static int collect_text_files(const dir_name_t edge,dir_name_t *names,unsigned limit,int forward)
{
 unsigned char *entry;
 unsigned count=0;
 if(bas_dir_open())return -1;
 while((entry=bas_dir_next())!=0){
  if((entry[13]&15)==2&&!staging_name(entry)&&name_matches(entry,bas_name)&&
     name_order(entry,edge,forward)>0){
   unsigned at=0,i;
   while(at<count&&name_order(entry,names[at],forward)>0)++at;
   if(at<limit){
    if(count<limit)++count;
    for(i=count-1;i>at;--i)copy_dir_name(names[i],names[i-1]);
    copy_dir_name(names[at],entry);
   }
  }
  byield();
 }
 return bas_dir_close()?-2:(int)count;
}

static int read_program_names(const dir_name_t edge,dir_name_t *names,unsigned limit,int forward)
{
 dir_name_t next;
 char name[BAS_NAME_MAX+1];
 unsigned count=0;
 copy_dir_name(next,edge);
 while(count<limit){
  unsigned start=count,i,room=limit-count;
  int got=collect_text_files(next,names+count,room,forward);
  if(got<0)return got;
  if(!got)break;
  copy_dir_name(next,names[start+got-1]);
  for(i=0;i<(unsigned)got;i++){
   decode_dir_name(name,names[start+i]);
   if(bas_read_source(name,CHECK_NUMBERED_TEXT))copy_dir_name(names[count++],names[start+i]);
  }
  if((unsigned)got<room)break;
 }
 return count;
}

/* Keep one extra row to know whether the current window reaches the end. */
#define DIR_WINDOW (BAS_PAGE_ROWS+1)

static int dir_forward(dir_name_t *names,unsigned count,unsigned step)
{
 unsigned kept,i;
 int got;
 if(count<=BAS_PAGE_ROWS)return 0;
 kept=count-step;
 for(i=0;i<kept;i++)copy_dir_name(names[i],names[i+step]);
 got=read_program_names(names[kept-1],names+kept,DIR_WINDOW-kept,1);
 return got<0?got:(int)kept+got;
}

static int dir_back(dir_name_t *names,unsigned count,unsigned step)
{
 dir_name_t previous[BAS_PAGE_ROWS];
 int got=read_program_names(names[0],previous,step,0);
 unsigned i,added;
 if(got<=0)return got;
 added=got;
 count+=added;
 if(count>DIR_WINDOW)count=DIR_WINDOW;
 for(i=count;i>added;--i)copy_dir_name(names[i-1],names[i-1-added]);
 for(i=0;i<added;i++)copy_dir_name(names[i],previous[added-1-i]);
 return count;
}

static void print_names(const dir_name_t *names,unsigned count)
{
 char name[BAS_NAME_MAX+1];
 unsigned i;
 for(i=0;i<count;i++){
  decode_dir_name(name,names[i]);
  outsc(name);outcr();
 }
}

static void directory_list(void)
{
 dir_name_t names[DIR_WINDOW],start={0};
 int count=read_program_names(start,names,DIR_WINDOW,1),status;
 if(count<0){puts(count==-1?"DIR open failed":"DIR close failed");return;}
 if(count<BAS_PAGE_ROWS){
  print_names(names,count);
  outnumber(count);outsc(" files\n");
  return;
 }
 page_begin();
 for(;;){
  page_clear();
  print_names(names,count>BAS_PAGE_ROWS?BAS_PAGE_ROWS:count);
  do {
   status=0;
   switch(page_key("DIR")){
   case 'Q':page_end();return;
   case 'A':status=dir_back(names,count,1);break;
   case 'B':status=dir_forward(names,count,1);break;
   case 'C':status=dir_forward(names,count,BAS_PAGE_ROWS);break;
   case 'D':status=dir_back(names,count,BAS_PAGE_ROWS);break;
   }
  } while(!status);
  if(status<0)break;
  count=status;
 }
 page_end();
 puts(status==-1?"DIR open failed":"DIR close failed");
}
unsigned bas_query(unsigned op)
{
 unsigned i;int status;
 bas_loaded=bas_new=bas_pending=0;
 if(op==BC_DIR){directory_list();return 0;}
 if(op==BC_SAVE&&!bas_name[0]){
  if(!bas_current[0]){puts("No current name; use .SAVE \"name\"");return 0;}
  for(i=0;i<11;i++)bas_name[i]=bas_current[i];
 }
 /* Reserve the staging namespace for the installer. */
 if(bas_name[0]=='B'&&bas_name[1]=='F'&&bas_name[2]=='R'&&bas_name[3]=='E'&&bas_name[4]=='E'){
  puts("Reserved BASIC staging name");return 0;
 }
 status=file_kind(bas_name);
 if(status!=2){
  if(status==-1&&op==BC_SAVE){
   if(pick_staging()){bas_new=1;return 1;}
   puts("No free BASIC file slot");
  }else if(status==-1)puts("File does not exist");
  else puts("Not a CENTOS text program file");
  return 0;
 }
 return 1;
}
void bas_publish(void)
{
 unsigned i;bas_pending=0;
 if(bas_dir_rename(bas_slot,bas_name)){puts("SAVE publication failed");return;}
 for(i=0;i<11;i++){bas_current[i]=bas_name[i];if(!bas_name[i])break;}
 puts("Saved");
}
void bas_save_file(void)
{
 int status,ok;const char *target=bas_new?bas_slot:bas_name;
 /* Preflight both source rendering and destination content before opening for
    output. New filenames are published only after a successful close. */
 if(!render_program(0)||er){puts("SAVE refused: line cannot be represented as text");return;}
 if(!bas_read_source(target,CHECK_SOURCE)||er){puts("SAVE refused: not an empty or numbered BASIC text file");return;}
 if(bas_io_open(target,2)||er){puts("SAVE open failed");return;}
 ok=render_program(1);status=bas_io_close();
 if(!ok||status||er){puts("SAVE write failed");return;}
 if(bas_new){bas_pending=1;return;}
 current_name();puts("Saved");
}

void bas_files(unsigned op)
{
 bas_loaded=0;bas_pending=0;
 if(op==BC_LOAD){
  if(!bas_read_source(bas_name,LOAD_SOURCE)){
   puts("LOAD failed: invalid, too large or unreadable source");
   return;
  }
  current_name();puts("Loaded");
 }else{
  bas_save_file();
 }
}
