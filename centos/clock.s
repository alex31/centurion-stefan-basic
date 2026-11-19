 .setcpu 6
 .code
 .export _bas_clock
; Native SVC 27 returns tenths of a second since midnight (32 bits).
_bas_clock:
 stx (-s)
 ldb 4(s)
 stb (clock_target)
 .byte 0x66,0x1b
clock_target:
 .word 0
 ldx (s+)
 rsr
