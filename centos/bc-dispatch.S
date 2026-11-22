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
    push xh,zl
    dcr s,2
    lda 10(s)           /* struct ib_cursor * */
    ldx (a)
    ldw y,2(a)
    ldw z,6(a)

poll:
    dcr z
    bz poll_yield
fetch:
    cla
    ldab (x)
    sub a,IB_COUNT-1,b
    bnl bad_op
    sla
    add a,handlers,b
    jmp @(b)
poll_yield:
    jsr _byield
    ldw z,64
    ldbb (_er)
    jnz stopped
    ldb (_stb_done)
    jnz stopped
    bra fetch
bad_op:
    jmp syntaxerr
handlers:
#define IB_OP(name, handler) .word handler
#include "bc-ops.def"
#undef IB_OP

constant:
    sub y,(_ib_stack_end),b
    jz stackerr
    ldb 1(x)
    inr x,3
    bra push_value
loadvar:
    sub y,(_ib_stack_end),b
    jz stackerr
    cla
    ldab 1(x)
    add _ib_state,a
    ldb (a)
    inr x,2
push_value:
    stb (y)
    inr y,4
    jmp poll

storevar:
    cla
    ldab 1(x)
    add _ib_state,a
    dcr y,4
    ldb (y)
    stb (a)
    inr x,2
    jmp poll

/* Real tokens, scalar slots and stack entries use the same four bytes. */
literalreal:
    sub y,(_ib_stack_end),b
    jz stackerr
    ldb 1(x)
    add _mem,b
    inr x,3
    bra push_real
loadreal:
    sub y,(_ib_stack_end),b
    jz stackerr
    cla
    ldab 1(x)
    add a,_ib_state,b
    inr x,2
push_real:
    lda (b+)
    sta (y+)
    lda (b)
    sta (y+)
    jmp poll

storereal:
    cla
    ldab 1(x)
    add a,_ib_state,b
    lda (-y)
    sta 2(b)
    lda (-y)
    sta (b)
    inr x,2
    jmp poll

isub:
    ldb -4(y)
    sub -8(y),b
    bra sum_done
iadd:
    ldb -8(y)
    add -4(y),b
sum_done:
    jn rangeerr         /* F is the signed overflow flag */
integer_done:
    stb -8(y)
    dcr y,4
    inx
    jmp poll

ineg:
    ivr -4(y),2         /* two's complement; -32768 stays unchanged on overflow */
    jn rangeerr
unary_done:
    inx
    jmp poll
inot:
    ivr -4(y)
    bra unary_done
iand:
    ldb -8(y)
    and -4(y),b
    bra integer_done
ior:
    ldb -8(y)
    ori -4(y),b
    bra integer_done

/* DIV returns the unsigned quotient in B and remainder in A. A signed
   quotient takes the XOR of the signs, a remainder the dividend's sign. */
idiv:
    ldb -4(y)
    jz divzeroerr
    lda -8(y)
    ore b,a
    sta (s)
    jsr divide_magnitudes
    lda (s)
    bm negate_division
    ori b,b
    jlt rangeerr        /* The only positive overflow is -32768 / -1. */
    bra integer_done
imod:
    ldb -4(y)
    jz divzeroerr
    jsr divide_magnitudes
    xab
    lda -8(y)
    bp integer_done
negate_division:
    ivr b,2
    bra integer_done

divide_magnitudes:
    lda -8(y)
    bp dividend_magnitude
    iva
    ina
dividend_magnitude:
    ori b,b
    bp divisor_magnitude
    ivr b,2
divisor_magnitude:
    div b,a
    rsr

/* A indexes a pair (base, exclusive bound), B is the subscript.
   JSR saves the bytecode IP on the stack. Borrow X for the descriptor
   and keep the caller's X in the scratch word; out-of-range aborts directly. */
element:
    stx 2(s)
    add a,(_ib_arrays),x
    lda 2(x)
    sub b,a             /* unsigned index - count */
    bl bad_element
    add b,b
    add 0(x),b
    ldx 2(s)
    rsr
bad_element:
    ldx (s+)            /* discard the subroutine link and restore the bytecode IP */
    jmp rangeerr

indexvar:
    sub y,(_ib_stack_end),b
    jz stackerr
    cla
    ldab 1(x)
    add _ib_state,a
    ldb (a)
    cla
    ldab 2(x)
    jsr element
    ldb (b)
    inr x,3
    stb (y)
    inr y,4
    jmp poll

indexoff:
    sub y,(_ib_stack_end),b
    jz stackerr
    cla
    ldab 1(x)
    add _ib_state,a
    ldb (a)
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
    ldb (b)
    inr x,4
    stb (y)
    inr y,4
    jmp poll

putvar:
    cla
    ldab 1(x)
    add _ib_state,a
    ldb (a)
    cla
    ldab 2(x)
    jsr element
    dcr y,4
    lda (y)
    sta (b)
    inr x,3
    jmp poll

putconst:
    cla
    ldab 3(x)
    add _ib_state,a
    ldb (a)
    cla
    ldab 4(x)
    jsr element
    lda 1(x)
    sta (b)
    inr x,5
    jmp poll

/* Both arrays have the same scalar index. Save the budget while Z
   holds that index and use the scratch word for the value being copied. */
copyvar:
    push zh,zl
    cla
    ldab 1(x)
    add _ib_state,a
    ldw z,0(a)
    cla
    ldab 2(x)
    add a,(_ib_arrays),b
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
    add a,(_ib_arrays),b
    lda 2(b)
    sub z,a
    bl copy_range
    add z,z
    add 0(b),z
    lda 2(s)
    sta (z)
    pop zh,zl
    inr x,4
    jmp poll
copy_range:
    pop zh,zl
    jmp rangeerr

jvar:
    cla
    ldab 1(x)
    add _ib_state,a
    lda (a)
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
    add b,_mem,x
    jmp poll

/* The next-free loop pointer lives in cursor.loops. The packed frame
   immediately before it holds limit, step, start and the tagged variable. */
nextloop:
    lda 10(s)
    ldb 4(a)
    sub b,(_ib_loops),a
    jz looperr
    stb (s)
    ldab -1(b)
    ldbb 1(x)
    subb bl,al
    jnz looperr
    clrb bh
    orib bl,bl
    bm next_real
    add _ib_state,b
    stb (-s)            /* address of the control variable */
    ldb (b)
    lda 2(s)
    add -7(a),b
    bf next_overflow
    lda (s+)
    stb (a)
    lda (s)
    ldab -7(a)
    bm next_down
    lda (s)
    sub -11(a),b        /* limit - current */
next_test:
    bf next_signed
    bp next_again
    bra next_done
next_down:
    lda (s)
    lda -11(a)
    sub b,a             /* current - limit */
    bra next_test
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
    add b,_mem,x
    jmp poll

next_real:
    ldab 127
    andb al,bl
    add _ib_state,b
    clr a,1
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
    bra finish
divzeroerr:
    ldb -8
finish:
    lda 10(s)
    stx (a)
    stw y,2(a)
    stw z,6(a)
    inr s,2
    pop xh,zl
    rsr

/* A one-character append to the last nonempty string only grows its
   descriptor and the heap tail. Let C handle all other cases, including
   compaction and error reporting, with X still on the same opcode. */
sappend:
    ldb 2(x)
    add _mem,b
    ldab (b)
    dcrb al
    jnz slow
    stb (s)
    push yh,yl
    cla
    ldab 1(x)
    sla
    sla
    add a,_ib_string_vars,y
    ldb 2(y)
    jz sappend_slow
    sub b,IB_STRING_LIMIT,a
    jz sappend_slow
    add 0(y),b
    add -IB_STRING_HEAP_REF,b
    sub b,(_ib_string_used),a
    jnz sappend_slow
    sub b,IB_STRING_HEAP_SIZE,a
    jz sappend_slow
    inr (_ib_string_used)
    add _ib_string_heap,b
    lda 2(s)
    ldab 1(a)
    stab (b)
    inr 2(y)
    pop yh,yl
    inr x,4
    jmp poll
sappend_slow:
    pop yh,yl
    jmp slow
; CPU6 function _ib_dispatch end
