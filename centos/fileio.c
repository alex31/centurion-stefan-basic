#include "fileio.h"
#include "work.h"
union bas_work_area bas_work;
struct bas_text_record bas_record;
#ifdef __CPU6_OS6__
int os6_assign_raw(char *,OS6_RCB *,int,int);
int os6_file_open_raw(OS6_RCB *,int);
int os6_file_read_raw(OS6_RCB *,void *);
int os6_file_write_raw(OS6_RCB *,void *);
int os6_file_end_raw(OS6_RCB *);
int os6_file_close_raw(OS6_RCB *);
#define storage bas_work
#define basic_file storage.file
static unsigned dir_offset,dir_sector;
static int dir_error;
#define LIBRARY_PREFIX_SIZE 7
static char dir_name[]={
 'S'|128,'B'|128,'A'|128,'S'|128,'I'|128,'C'|128,'.'|128,
 '@'|128,'P'|128,'D'|128,'I'|128,'R'|128,'0'|128,0
};
static int dir_open(unsigned mode)
{
 unsigned i;int status;OS6_RCB *r=&storage.directory.rcb;unsigned char *p=(unsigned char *)r;
 for(i=0;i<sizeof(*r);i++)p[i]=0;
 r->logical_unit=0;r->control=0x2101;r->buffer_size=400;
 r->buffer=(uint8_t *)&storage.directory.block.length;r->record_flags=5;r->class_code=1;r->file_type=255;
 r->reserved2=(uint16_t)storage.directory.position;
 storage.directory.position[0]=storage.directory.position[1]=0;
 storage.directory.block.capacity=storage.directory.block.length=400;
 status=os6_assign_raw(dir_name,r,1,0);if(status)return status;
 status=os6_file_open_raw(r,(int)mode);
 if(status)os6_assign_raw(0,r,1,0);
 dir_offset=400;dir_sector=0;dir_error=0;return status;
}
int bas_dir_open(void){return dir_open(1);}
unsigned char *bas_dir_next(void)
{
 unsigned char *p;int status;
 for(;;){
  if(dir_offset==400){
   storage.directory.position[0]=0;storage.directory.position[1]=dir_sector;
   storage.directory.block.capacity=storage.directory.block.length=400;
   status=os6_file_read_raw(&storage.directory.rcb,&storage.directory.block.length);
   if(status){dir_error=status==1?0:status;return 0;}
   dir_offset=dir_sector?0:16;dir_sector++;
  }
  p=storage.directory.block.data+dir_offset;dir_offset+=16;
  if(p[0]==0x84&&p[1]==0x8d)return 0;
  if(p[0])return p;
 }
}
int bas_dir_close(void)
{
 int status=os6_file_close_raw(&storage.directory.rcb),next;
 next=os6_assign_raw(0,&storage.directory.rcb,1,0);return dir_error?dir_error:(status?status:next);
}
static int dir_name_equal(const unsigned char *entry,const char *name)
{
 unsigned i=0;while(i<10&&name[i]){if((entry[i]&127)!=(unsigned char)name[i])return 0;i++;}
 if(i==10)return name[i]==0;
 while(i<10)if((entry[i++]&127)!=' ')return 0;
 return 1;
}
/* Publish a previously allocated, closed BASIC staging file. Allocation maps
   and file extents stay unchanged. Recheck both names immediately before the
   one-sector directory update; never replace an existing destination. */
int bas_dir_rename(const char *from,const char *to)
{
 unsigned char *entry;unsigned sector=65535U,offset=0,i,ended=0;int status,close_status;
 if(dir_open(3))return -1;
 while((entry=bas_dir_next())!=0){
  if(dir_name_equal(entry,to)){bas_dir_close();return -1;}
  if(dir_name_equal(entry,from)&&(entry[13]&15)==2){sector=dir_sector-1;offset=dir_offset-16;}
 }
 if(dir_error||sector==65535U){bas_dir_close();return -1;}
 storage.directory.position[0]=0;storage.directory.position[1]=sector;storage.directory.block.capacity=storage.directory.block.length=400;
 status=os6_file_read_raw(&storage.directory.rcb,&storage.directory.block.length);
 entry=storage.directory.block.data+offset;
 if(status||!dir_name_equal(entry,from)||(entry[13]&15)!=2){bas_dir_close();return -1;}
 for(i=0;i<10;i++){
  if(!ended&&!to[i])ended=1;
  entry[i]=(ended?' ':(unsigned char)to[i])|128;
 }
 storage.directory.position[0]=0;storage.directory.position[1]=sector;storage.directory.block.capacity=storage.directory.block.length=400;
 status=os6_file_write_raw(&storage.directory.rcb,&storage.directory.block);
 close_status=bas_dir_close();return status?status:close_status;
}
/* LSR 5 transfers the native 400-byte file sectors. Type A records are the
   usual high-bit ASCII, CR separators and EOT/CR terminator, also used by TXT.
   READB's raw address starts at length; WRITEB starts at capacity. */
#define text_file storage.text
static int text_error;
int bas_io_open(const char *name,unsigned mode)
{
 unsigned i;int status;char encoded[LIBRARY_PREFIX_SIZE+11];OS6_RCB *r=&text_file.rcb;
 unsigned char *raw=(unsigned char *)r;
 for(i=0;i<sizeof(*r);i++)raw[i]=0;
 for(i=0;i<LIBRARY_PREFIX_SIZE;i++)encoded[i]=dir_name[i];
 for(i=0;i<10&&name[i];i++)encoded[LIBRARY_PREFIX_SIZE+i]=(unsigned char)name[i]|128;
 encoded[LIBRARY_PREFIX_SIZE+i]=0;text_file.mode=mode;text_file.offset=mode==1?400:0;text_error=0;
 r->control=0x2101;r->buffer_size=400;r->buffer=(uint8_t *)&text_file.block.length;
 r->record_flags=5;r->class_code=1;r->file_type=255;
 r->reserved2=(uint16_t)text_file.position;
 text_file.position[0]=text_file.position[1]=0;
 text_file.block.capacity=text_file.block.length=400;
 status=os6_assign_raw(encoded,r,1,0);if(status)return status;
 status=os6_file_open_raw(r,mode);
 if(status)os6_assign_raw(0,r,1,0);return status;
}
int bas_io_read(void)
{
 unsigned c,n=0;int status;
 if(text_error)return text_error;
 for(;;){
  if(text_file.offset==400){
   text_file.block.capacity=text_file.block.length=400;
   status=os6_file_read_raw(&text_file.rcb,&text_file.block.length);
   if(status)return status;
   text_file.position[1]++;text_file.offset=0;
  }
  c=text_file.block.data[text_file.offset++]&127;
  if(c==4){text_error=1;if(n)break;return 1;}
  /* An empty record (the second CR) ends a native type A sector. */
  if(c==13){if(n)break;text_file.offset=400;continue;}
  if(!c||c==10)continue;
  if(n==BAS_TEXT_MAX)return -1;bas_record.data[n++]=(unsigned char)c;
 }
 bas_record.length=n;bas_record.data[n]=0;return 0;
}
static int flush_text(void)
{
 int status;text_file.block.capacity=text_file.block.length=400;
 status=os6_file_write_raw(&text_file.rcb,&text_file.block);
 if(status){text_error=status;return status;}
 text_file.position[1]++;text_file.offset=0;return 0;
}
static int put_text(unsigned c)
{
 if(text_error)return text_error;
 text_file.block.data[text_file.offset++]=(unsigned char)c|128;
 return text_file.offset==400?flush_text():0;
}
int bas_io_write(void)
{
 unsigned i;
 /* LIOCS uses an extra CR after the last record as its end-of-sector marker.
    Always reserve that byte: zero padding alone is not a valid terminator. */
 if(bas_record.length+2>400-text_file.offset){
  text_file.block.data[text_file.offset++]=0x8d;
  while(text_file.offset<400)text_file.block.data[text_file.offset++]=0;
  if(flush_text())return text_error;
 }
 for(i=0;i<bas_record.length;i++)if(put_text(bas_record.data[i]))return text_error;
 return put_text(13);
}
/* Append the native EOT record while the writer's overlay is active. */
int bas_finish_text(void)
{
 if(text_file.offset==399){text_file.block.data[text_file.offset++]=0x8d;flush_text();}
 put_text(4);put_text(13);
 if(!text_error&&text_file.offset){while(text_file.offset<400)text_file.block.data[text_file.offset++]=0;flush_text();}
 return text_error;
}
int bas_io_close(void)
{
 int status=text_file.mode==2?bas_finish_text():0,next;
 next=os6_file_close_raw(&text_file.rcb);if(!status)status=next;
 next=os6_assign_raw(0,&text_file.rcb,1,0);return status?status:next;
}

#else
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
static DIR *directory;
static unsigned char dir_entry[16];
int bas_dir_open(void){directory=opendir(".");return directory?0:-1;}
unsigned char *bas_dir_next(void){struct dirent *entry;struct stat info;unsigned i,n;
 while((entry=readdir(directory))!=0){
  n=(unsigned)strlen(entry->d_name);if(n>10||entry->d_name[0]=='.')continue;
  if(stat(entry->d_name,&info)||!S_ISREG(info.st_mode))continue;
  for(i=0;i<16;i++)dir_entry[i]=0;
  for(i=0;i<10;i++)dir_entry[i]=(i<n?(unsigned char)entry->d_name[i]:' ')|128;
  dir_entry[13]=2;return dir_entry;
 }return 0;
}
int bas_dir_close(void){int status=closedir(directory);directory=0;return status;}
int bas_dir_rename(const char *from,const char *to){struct stat info;
 if(!stat(to,&info))return -1;return rename(from,to);}
static FILE *basic_file;
int bas_io_open(const char *name,unsigned mode){basic_file=fopen(name,mode==1?"rb":"wb");return basic_file?0:2;}
int bas_io_read(void)
{
 int c;unsigned n=0;while((c=fgetc(basic_file))!=EOF&&c!='\n'&&c!='\r'){
  if(n==BAS_TEXT_MAX)return -1;bas_record.data[n++]=(unsigned char)c;
 }
 if(c=='\r'){c=fgetc(basic_file);if(c!=EOF&&c!='\n')ungetc(c,basic_file);}
 if(ferror(basic_file))return -1;
 if(c==EOF&&!n)return 1;
 bas_record.length=n;bas_record.data[n]=0;return 0;
}
int bas_io_write(void){return fwrite(bas_record.data,1,bas_record.length,basic_file)==bas_record.length&&fputc('\n',basic_file)!=EOF?0:-1;}
int bas_io_close(void){int status=fclose(basic_file);basic_file=0;return status;}
#endif
