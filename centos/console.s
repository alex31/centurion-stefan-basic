 .setcpu 6
 .code
 .export _bas_readline
 .export _bas_rawkey
 .export _bas_ov_abort
; A failed caller-code restoration must never return into the wrong overlay.
_bas_ov_abort:
 .byte 0x66,0x0a,253
 bra _bas_ov_abort
; Canonical native CRT input. The OS edits/echoes the whole line and returns
; its actual length, including trailing spaces, in a two-byte prefix.
; Caller provides limit+3 bytes: length16, text[limit], NUL.
_bas_readline:
 stx (-s)
 ldb 4(s)
 stb (line_buffer)
 ldb 6(s)
 stb (line_limit)
 ldb line_op
 .byte 0x66,0x00
 .byte 0x66,0x08
 .word line_op
 ldb (line_buffer)
 ldb (b)
 ldx (s+)
 rsr
; Read one CRT byte without waiting for a line. CENTOS echoes and translates keys.
_bas_rawkey:
 stx (-s)
 ldb key_op
 .byte 0x66,0x00
 .byte 0x66,0x08
 .word key_op
 ldbb (key_byte+2)
 clrb bh
 ldx (s+)
 rsr
 .data
line_op:
 .byte 0,0x81,0,1
line_limit:
 .word 0
line_buffer:
 .word 0,0
key_op:
 .byte 0,0x81,0x08,1
 .word 1,key_byte,0
 .bss
key_byte:
 .ds 4
