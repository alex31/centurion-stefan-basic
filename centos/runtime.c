/* Console adapter for the original tokenized Stefan BASIC core. */
#ifndef __SDCC
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#endif
#include "hardware.h"
#include "runtime.h"
#ifdef __CPU6_OS6__
#include <os6.h>
int bas_readline(char *,unsigned);
#endif

int8_t id=1,od=1,idd=1,odd=1,ioer=0;
int stb_done=0;
#ifdef __CPU6_OS6__
void bas_clock(uint32_t *);
static uint32_t clock_start;
void timeinit(void) {bas_clock(&clock_start);}
unsigned long millis(void) {
    uint32_t now;bas_clock(&now);
    if(now<clock_start)now+=864000UL;
    return (now-clock_start)*100UL;
}
#elif !defined(__SDCC)
static struct timespec clock_start;
void timeinit(void) {clock_gettime(CLOCK_MONOTONIC,&clock_start);}
unsigned long millis(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (unsigned long)((now.tv_sec-clock_start.tv_sec)*1000L+(now.tv_nsec-clock_start.tv_nsec)/1000000L);
}
#else
/* Bare CPU core qualification has no OS clock. */
void timeinit(void) {}
unsigned long millis(void) {return 0;}
#endif

void iodefaults(void) { id=idd;od=odd; }
void ioinit(void) { iodefaults(); }
int iostat(int channel) { return channel==1; }
void byield(void) {
#ifdef __CPU6_OS6__
    os6_checkpoint();
#endif
}
void bdelay(uint32_t delay) {uint32_t start=millis();while((uint32_t)(millis()-start)<delay)byield();}
void fastticker(void) { byield(); }
void breakpinbegin(void) {}
void serialflush(void) {}
void ofileclose(void) {}
void restartsystem(void) { stb_done=1; }
char checkch(void) { return 0; }
uint16_t availch(void) { return 0; }
uint8_t dspactive(void) { return 0; }
char dspwaitonscroll(void) { return 0; }
uint16_t elength(void) { return 0; }
int8_t eread(uint16_t address) { (void)address;return 0; }
void eupdate(uint16_t address,int8_t value) { (void)address;(void)value; }
void eflush(void) {}
uint8_t netconnected(void) { return 0; }
uint8_t mqttstate(void) { return 0; }
uint16_t aread(uint8_t pin) { (void)pin;return 0; }
uint8_t dread(uint8_t pin) { (void)pin;return 0; }
void awrite(uint8_t pin,uint16_t value) { (void)pin;(void)value; }
void dwrite(uint8_t pin,uint8_t value) { (void)pin;(void)value; }
void pinm(uint8_t pin,uint8_t mode) { (void)pin;(void)mode; }
void portwrite(uint8_t pin,int value) { (void)pin;(void)value; }
void ddrwrite(uint8_t pin,int value) { (void)pin;(void)value; }
int portread(uint8_t pin) { (void)pin;return 0; }
int ddrread(uint8_t pin) { (void)pin;return 0; }
int pinread(uint8_t pin) { (void)pin;return 0; }
extern char ibuffer[BUFSIZE];
void outch(char c) {
    if(od==0) {
        unsigned length=(unsigned char)ibuffer[0];
        /* Match the original buffer device: listing line breaks are ignored
           while text is captured for EDIT; form feed clears and BS deletes. */
        if(c==10||c==13)return;
        if(c==12){ibuffer[0]=ibuffer[1]=0;return;}
        if(c==8){if(length)length--;ibuffer[0]=(char)length;ibuffer[length+1]=0;return;}
        if(length+2<BUFSIZE&&length<127) {
            ibuffer[++length]=c;ibuffer[0]=(char)length;ibuffer[length+1]=0;
        } else ioer=1;
    } else bas_emit(c);
}
void outs(char *s,uint16_t n) { while(n--)outch(*s++); }
#ifdef __SDCC
int abs(int value) { return value<0?-value:value; }
void *__memcpy(void *destination,const void *source,unsigned int count) {
    unsigned char *d=destination;
    const unsigned char *s=source;
    while(count--)*d++=*s++;
    return destination;
}
#endif

#ifdef STBASIC_SCRIPT
static const char input_script[]=
 "10 S=0\n20 FOR I=1 TO 10\n30 S=S+I\n40 NEXT I\n50 PRINT S\nRUN\n"
 "PRINT 3*(7+5)\n"
#ifndef STEFAN_TINY
 "DIM A(4)\nA(2)=123\nPRINT A(2)\nB$=\"CENTURION\"\nPRINT B$\n"
#endif
 "PRINT \"PASS STEFAN BASIC\"\nBYE\n";
static unsigned input_pos;
char inch(void) { return input_script[input_pos] ? input_script[input_pos++] : 0; }
#elif defined(__CPU6_OS6__)
char inch(void) { return 0; }
#else
char inch(void) { int c=getchar();if(c==EOF){stb_done=1;return 0;}return (char)c; }
#endif

uint16_t ins(char *buffer,uint16_t capacity) {
    uint16_t n=0;
    bas_flush();
    if(capacity<2)return 0;
#if defined(__CPU6_OS6__) && !defined(STBASIC_SCRIPT)
    unsigned i;
    int read;
    if(capacity<3){buffer[0]=buffer[1]=0;return 0;}
    read=bas_readline(buffer,capacity-3);
    if(read<0||(unsigned)read>capacity-3){stb_done=1;buffer[0]=buffer[1]=0;return 0;}
    n=(uint16_t)read;
    /* Native count16/text becomes the core's count8/text in place. */
    for(i=0;i<n;i++)buffer[i+1]=buffer[i+2]&127;
#else
    char c;
    while((c=inch()) && c!='\n') {
        if(c!='\r' && n+3<capacity)buffer[1+n++]=c;
    }
    if(!c)stb_done=1;
#endif
    buffer[0]=(char)n;buffer[n+1]=0;
    if(n==3 && buffer[1]=='B' && buffer[2]=='Y' && buffer[3]=='E') {
        stb_done=1;buffer[0]=buffer[1]=0;n=0;
    }
    return n;
}
uint16_t consins(char *buffer,uint16_t capacity) { return ins(buffer,capacity); }

/* Batch normal terminal output; the program is still token-only. One
   WRITEN per chunk avoids hundreds of SVCs during each editor repaint. */
#ifdef __CPU6_OS6__
static unsigned char terminal_output[65];
static unsigned terminal_used;
void os6_write_encoded(unsigned char *,unsigned);
void bas_flush(void) {
    unsigned n=terminal_used;
    while(n&&terminal_output[n-1]==0xa0U)n--;
    if(n){terminal_output[n]=0;os6_write_encoded(terminal_output,n);}
    while(n<terminal_used){putchar(' ');n++;}
    terminal_used=0;
}
void bas_emit(char c) {
    if(c=='\n'){bas_flush();putchar('\n');return;}
    terminal_output[terminal_used++]=(unsigned char)c|128;
    if(terminal_used==64)bas_flush();
}
#else
void bas_flush(void) {}
void bas_emit(char c) {putchar((unsigned char)c);}
#endif
