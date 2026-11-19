#include "app.h"
#include "fileio.h"
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
int bas_read_source(const char *name,unsigned replace)
{
 struct source_check original,copied;
 if(bas_io_open(name,1)||!read_and_close(0,&original))return 0;
 if(!replace)return 1;
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
static void directory_list(void)
{
 unsigned char *entry;unsigned i,count=0;
 if(bas_dir_open()){puts("DIR open failed");return;}
 while((entry=bas_dir_next())!=0){
  if(!staging_name(entry)&&name_matches(entry,bas_name)){
   for(i=0;i<10&&(entry[i]&127)!=' ';i++)outch(entry[i]&127);
   outcr();count++;
  }
  byield();
 }
 if(bas_dir_close()){puts("DIR close failed");return;}
 outnumber(count);outsc(" files\n");
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
 if(!bas_read_source(target,0)||er){puts("SAVE refused: not an empty or numbered BASIC text file");return;}
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
  if(!bas_read_source(bas_name,1)){
   puts("LOAD failed: invalid, too large or unreadable source");
   return;
  }
  current_name();puts("Loaded");
 }else{
  bas_save_file();
 }
}
