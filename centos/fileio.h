#ifndef BASIC_FILEIO_H
#define BASIC_FILEIO_H
#include <stdint.h>
#define BAS_TEXT_MAX 398
/* One text record shared by SAVE rendering and LOAD tokenization. */
extern struct bas_text_record {
 uint16_t capacity,length;
 unsigned char data[BAS_TEXT_MAX+1];
} bas_record;
int bas_io_open(const char *,unsigned);
int bas_io_read(void);
int bas_io_write(void);
int bas_io_close(void);
int bas_dir_open(void);
unsigned char *bas_dir_next(void);
int bas_dir_close(void);
int bas_dir_rename(const char *,const char *);
#endif
