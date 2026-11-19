#ifndef BASIC_WORK_AREA_H
#define BASIC_WORK_AREA_H
#include <stdint.h>
#include "bytecode.h"
#ifdef __CPU6_OS6__
#include <os6file.h>
#endif
/* Editing and disk commands are mutually exclusive. Reuse the existing file
   workspace for the character origins and numeric-token locations of one line. */
struct bas_literal {
 uint16_t address;
 unsigned char start,length;
};
union bas_work_area {
 uint32_t alignment;
 unsigned char bytes[448];
 struct {unsigned char origin[128];struct bas_literal numbers[64];unsigned count;} editor;
#ifdef __CPU6_OS6__
 FILE_OS6 file;
 struct {
  OS6_RCB rcb;
  uint16_t position[2];
  struct {uint16_t capacity,length;unsigned char data[400];} block;
 } directory;
 struct {
  OS6_RCB rcb;
  uint16_t position[2];
  /* LIOCS appends a NUL after its 400-byte buffer, including during OPEN. */
  struct {uint16_t capacity,length;unsigned char data[400],terminator;} block;
  uint16_t offset,mode;
 } text;
#endif
};
extern union bas_work_area bas_work;
typedef char bas_work_size_check[sizeof(union bas_work_area)==448?1:-1];
#endif
