/* The CPU6 keeps the bytecode address in X, the next free value in Y and
   the remaining checkpoint budget in Z. Return at the first C opcode, with
   that opcode still under X. Branch operands are big-endian arena offsets. */
#include "bc-string-layout.h"

    .setcpu 6
    .code
    .export _ib_dispatch
; CPU6 function _ib_dispatch begin
#define IB_OP(name, handler) IB_##name equ __COUNTER__
#include "bc-ops.def"
#undef IB_OP
IB_COUNT equ __COUNTER__

_ib_dispatch:
    stx (-s)
    xfr y,a
    sta (-s)
    xfr z,a
    sta (-s)
    dcr s,2
    lda 10(s)           /* struct ib_cursor * */
    ldx (a)
    ldb 2(a)
    xfr b,y
    ldb 6(a)
    xfr b,z

poll:
    dcr z
    bnz fetch
    jsr _byield
    ldb 64
    xfr b,z
    ldbb (_er)
    jnz stopped
    ldb (_stb_done)
    jnz stopped
fetch:
    ldbb (x)
    clrb bh
    slr b
    lda handlers
    aab
    ldb (b)
    jmp (b)
handlers:
#define IB_OP(name, handler) .word handler
#include "bc-ops.def"
#undef IB_OP
#include "bc-invalid.inc"

constant:
    xfr y,a
    ldb (_ib_stack_end)
    sab
    jz stackerr
    ldb 1(x)
    inr x,3
    bra push_value
loadvar:
    xfr y,a
    ldb (_ib_stack_end)
    sab
    jz stackerr
    clrb bh
    ldbb 1(x)
    lda _ib_state
    aab
    ldb (b)
    inr x,2
push_value:
    stb (y)
    inr y,4
    jmp poll

storevar:
    cla
    ldab 1(x)
    ldb _ib_state
    aab
    xfr b,a
    dcr y,4
    ldb (y)
    stb (a)
    inr x,2
    jmp poll

/* Real tokens, scalar slots and stack entries use the same four bytes. */
literalreal:
    xfr y,a
    ldb (_ib_stack_end)
    sab
    jz stackerr
    lda _mem
    ldb 1(x)
    aab
    inr x,3
    bra push_real
loadreal:
    xfr y,a
    ldb (_ib_stack_end)
    sab
    jz stackerr
    cla
    ldab 1(x)
    ldb _ib_state
    aab
    inr x,2
push_real:
    lda (b)
    sta (y)
    lda 2(b)
    sta 2(y)
    inr y,4
    jmp poll

storereal:
    cla
    ldab 1(x)
    ldb _ib_state
    aab
    dcr y,4
    lda (y)
    sta (b)
    lda 2(y)
    sta 2(b)
    inr x,2
    jmp poll

iadd:
    lda -8(y)
    ldb -4(y)
    aab
    bra sum_done
isub:
    lda -8(y)
    ldb -4(y)
    sab
sum_done:
    jn rangeerr         /* F is the signed overflow flag */
    stb -8(y)
    dcr y,4
    inx
    jmp poll

/* A indexes a pair (base, exclusive bound), B is the subscript.
   JSR saves the bytecode IP on the stack. Borrow X for the descriptor
   and keep the local return link in the caller's scratch word. */
element:
    stx 2(s)
    ldx (_ib_arrays)
    add a,x
    lda 2(x)
    sub b,a             /* unsigned index - count */
    bl bad_element
    lda (x)
    slr b
    aab
    ldx 2(s)
    rsr
bad_element:
    clr b
    ldx 2(s)
    rsr

indexvar:
    xfr y,a
    ldb (_ib_stack_end)
    sab
    jz stackerr
    cla
    ldab 1(x)
    ldb _ib_state
    aab
    ldb (b)
    cla
    ldab 2(x)
    jsr element
    ori b,b
    jz rangeerr
    ldb (b)
    inr x,3
    jmp push_value

indexoff:
    xfr y,a
    ldb (_ib_stack_end)
    sab
    jz stackerr
    cla
    ldab 1(x)
    ldb _ib_state
    aab
    ldb (b)
    cla
    ldab 3(x)
    bp add_offset
    dcrb ah
add_offset:
    aab
    jn rangeerr
    cla
    ldab 2(x)
    jsr element
    ori b,b
    jz rangeerr
    ldb (b)
    inr x,4
    jmp push_value

putvar:
    cla
    ldab 1(x)
    ldb _ib_state
    aab
    ldb (b)
    cla
    ldab 2(x)
    jsr element
    ori b,b
    jz rangeerr
    dcr y,4
    lda (y)
    sta (b)
    inr x,3
    jmp poll

putconst:
    cla
    ldab 3(x)
    ldb _ib_state
    aab
    ldb (b)
    cla
    ldab 4(x)
    jsr element
    ori b,b
    jz rangeerr
    lda 1(x)
    sta (b)
    inr x,5
    jmp poll

/* Both arrays have the same scalar index. Save the budget while Z
   holds that index and use the scratch word for the value being copied. */
copyvar:
    xfr z,a
    sta (-s)
    cla
    ldab 1(x)
    ldb _ib_state
    aab
    ldb (b)
    xfr b,z
    cla
    ldab 2(x)
    ldb (_ib_arrays)
    aab
    lda 2(b)
    sub z,a
    bl copy_range
    lda (b)
    add z,a
    add z,a
    lda (a)
    sta 2(s)
    cla
    ldab 3(x)
    ldb (_ib_arrays)
    aab
    lda 2(b)
    sub z,a
    bl copy_range
    lda (b)
    add z,a
    add z,a
    ldb 2(s)
    stb (a)
    lda (s+)
    xaz
    inr x,4
    jmp poll
copy_range:
    lda (s+)
    xaz
    jmp rangeerr

jvar:
    cla
    ldab 1(x)
    ldb _ib_state
    aab
    lda (b)
    ldb 2(x)
    inr x,5
    bra condition
jcmp:
    dcr y,8
    lda (y)
    ldb 4(y)
    inr x,2
condition:
    sab
    bz equal
    bf invert_sign
    bm less
    bra greater
invert_sign:
    bp less
greater:
    ldab 4
    bra test_mask
less:
    ldab 1
    bra test_mask
equal:
    ldab 2
test_mask:
    ldbb -1(x)
    andb al,bl
    bz read_target
    inr x,2
    jmp poll

jzero:
    ldab 1(x)
    dcrb al             /* IB_INT = 1 */
    jnz slow
    dcr y,4
    ldb (y)
    bz bool_jump
    inr x,4
    jmp poll
jnz_vm:
    ldab 1(x)
    dcrb al
    jnz slow
    dcr y,4
    ldb (y)
    bnz bool_jump
    inr x,4
    jmp poll
bool_jump:
    inx
jump:
    inx
read_target:
    ldb (x)
set_ip:
    lda _mem
    aab
    xfr b,x
    jmp poll

/* The next-free loop pointer lives in cursor.loops. The packed frame
   immediately before it holds limit, step, start and the tagged variable. */
nextloop:
    lda 10(s)
    ldb 4(a)
    lda (_ib_loops)
    sub b,a
    jz looperr
    stb (s)
    ldab -1(b)
    ldbb 1(x)
    subb al,bl
    jnz looperr
    clrb bh
    ldbb 1(x)
    bm next_real
    lda _ib_state
    aab
    stb (-s)            /* address of the control variable */
    lda (b)
    ldb 2(s)
    ldb -7(b)
    aab
    bf next_overflow
    lda (s+)
    stb (a)
    lda (s)
    ldab -7(a)
    bm next_down
    lda (s)
    lda -11(a)
    sab                 /* limit - current */
    bra next_test
next_down:
    lda (s)
    lda -11(a)
    sub b,a             /* current - limit */
next_test:
    bf next_signed
    bp next_again
    bra next_done
next_signed:
    bm next_again
    bra next_done
next_overflow:
    inr s,2             /* leave the last representable value in the variable */
next_done:
    ldb (s)
    dcr b,11
    lda 10(s)
    stb 4(a)
    inr x,2
    jmp poll
next_again:
    ldb (s)
    ldb -3(b)
    bra set_ip

next_real:
    ldab 127
    andb al,bl
    lda _ib_state
    aab
    lda 1
    sta (-s)            /* advance */
    stb (-s)            /* variable */
    ldb 4(s)
    dcr b,11
    stb (-s)            /* loop */
    jsr _ib_for_real
    inr s,6
    ori b,b
    jlt rangeerr
    bnz next_again
    bra next_done

slow:
    clrb bh
    ldbb (x)
    bra finish
stopped:
    ldb IB_COUNT
    bra finish
syntaxerr:
    ldb -1
    bra finish
rangeerr:
    ldb -4
    bra finish
looperr:
    ldb -6
    bra finish
stackerr:
    ldb -7
finish:
    stb (s)
    lda 10(s)
    stx (a)
    xfr y,b
    stb 2(a)
    xfr z,b
    stb 6(a)
    ldb (s+)
    lda (s+)
    xaz
    lda (s+)
    xay
    ldx (s+)
    rsr

/* A one-character append to the last nonempty string only grows its
   descriptor and the heap tail. Let C handle all other cases, including
   compaction and error reporting, with X still on the same opcode. */
sappend:
    lda _mem
    ldb 2(x)
    aab
    ldab (b)
    dcrb al
    jnz slow
    stb (s)
    xfr y,a
    sta (-s)
    clrb bh
    ldbb 1(x)
    slr b
    slr b
    lda _ib_string_vars
    aab
    xfr b,y
    ldb 2(y)
    ori b,b
    jz sappend_slow
    lda IB_STRING_LIMIT
    sab
    jz sappend_slow
    lda (y)
    ldb 2(y)
    aab
    lda IB_STRING_HEAP_REF
    sub b,a             /* tagged end - heap reference tag */
    ldb (_ib_string_used)
    sub b,a
    jnz sappend_slow
    lda IB_STRING_HEAP_SIZE
    sub b,a
    jz sappend_slow
    inr b
    stb (_ib_string_used)
    dcr b
    lda _ib_string_heap
    aab
    lda 2(s)
    ldab 1(a)
    stab (b)
    ldb 2(y)
    inr b
    stb 2(y)
    lda (s+)
    xay
    inr x,4
    jmp poll
sappend_slow:
    lda (s+)
    xay
    jmp slow
; CPU6 function _ib_dispatch end
