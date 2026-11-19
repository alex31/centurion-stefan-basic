#include "bytecode.h"
#include "work.h"

#ifdef STBASIC_VM_ASM
struct ib_cursor {
    const uint8_t *ip;
    ib_value *sp;
    struct ib_loop *loops;
    uint16_t budget;
};
int ib_dispatch(struct ib_cursor *);
#define IB_MEMBER(member) ((uint8_t *)&bas_work + __builtin_offsetof(struct ib_state, member))
ib_value *const ib_stack_end = (ib_value *)(IB_MEMBER(stack) + STACKSIZE * sizeof(ib_value));
struct ib_array *const ib_arrays = (struct ib_array *)IB_MEMBER(array);
struct ib_loop *const ib_loops = (struct ib_loop *)IB_MEMBER(loops);
/* Layout shared with bc-dispatch.S. All persistent BASIC storage still
   belongs to the arena or the 448-byte workspace. */
typedef char ib_dispatch_layout[
    sizeof(ib_value) == 4 && sizeof(struct ib_array) == 4 &&
    __builtin_offsetof(struct ib_array, data) == 0 && __builtin_offsetof(struct ib_array, count) == 2 &&
    __builtin_offsetof(struct ib_cursor, ip) == 0 && __builtin_offsetof(struct ib_cursor, sp) == 2 &&
    __builtin_offsetof(struct ib_cursor, loops) == 4 && __builtin_offsetof(struct ib_cursor, budget) == 6 &&
    sizeof(struct ib_loop) == 11 && __builtin_offsetof(struct ib_loop, limit) == 0 &&
    __builtin_offsetof(struct ib_loop, step) == 4 && __builtin_offsetof(struct ib_loop, start) == 8 &&
    __builtin_offsetof(struct ib_loop, variable) == 10 && IB_INT == 1 && IB_SYNTAX == 1 &&
    IB_RANGE == 4 && IB_LOOP == 6 && IB_STACK == 7 ? 1 : -1];
#endif

static inline unsigned vm_word(const uint8_t *p)
{
#ifdef STBASIC_VM_ASM
    return __cpu6_asm16("ldb (b)", (unsigned)p);
#else
    return ((unsigned)p[0] << 8) | p[1];
#endif
}

static inline void vm_write_word(uint8_t *p, unsigned value)
{
#ifdef STBASIC_VM_ASM
    __cpu6_asm16_2("stb (a)", (unsigned)p, value);
#else
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
#endif
}

static inline ib_value *scalar_at(unsigned offset)
{
    return (ib_value *)((uint8_t *)bas_work.ib.scalar + offset);
}

static int integer_compare(unsigned op, int16_t a, int16_t b)
{
    switch (op) {
    case IB_IEQ: return a == b;
    case IB_INE: return a != b;
    case IB_ILT: return a < b;
    case IB_ILE: return a <= b;
    case IB_IGT: return a > b;
    case IB_IGE: return a >= b;
    }
    return 0;
}

static inline int matches(unsigned mask, int16_t a, int16_t b)
{
    return mask & (a < b ? 1u : a == b ? 2u : 4u);
}

#ifdef STBASIC_VM_ASM
int ib_add16(int16_t *, int16_t);
int ib_sub16(int16_t *, int16_t);
int ib_mul16(int16_t *, int16_t);
#else
/* Unsigned arithmetic also covers the two signed endpoints on 16-bit hosts. */
static int ib_add16(int16_t *value, int16_t step)
{
    uint16_t a = (uint16_t)*value, b = (uint16_t)step, sum = a + b;
    if ((~(a ^ b) & (a ^ sum)) & 0x8000u)
        return 0;
    *value = (int16_t)sum;
    return 1;
}

static int ib_sub16(int16_t *value, int16_t other)
{
    uint16_t a = (uint16_t)*value, b = (uint16_t)other, difference = a - b;
    if (((a ^ b) & (a ^ difference)) & 0x8000u)
        return 0;
    *value = (int16_t)difference;
    return 1;
}

static int ib_mul16(int16_t *value, int16_t other)
{
    int32_t product = (int32_t)*value * other;
    if (product < -32768L || product > 32767L)
        return 0;
    *value = (int16_t)product;
    return 1;
}
#endif

static int loop_in_range(struct ib_loop *loop, ib_value *var, unsigned advance)
{
    int16_t step;
    if (loop->variable & 128)
        return ib_for_real(loop, var, advance);
    step = loop->step.integer;
    if (!step)
        return -1;
    if (advance && !ib_add16(&var->integer, step))
        return 0;
    return step > 0 ? var->integer <= loop->limit.integer : var->integer >= loop->limit.integer;
}

static unsigned error_line(const struct ib_program *p, unsigned pc)
{
    const uint8_t *slot = (const uint8_t *)mem + p->table;
    unsigned i, line = 0;
    for (i = 0; i < p->lines; ++i, slot += 4) {
        if (vm_word(slot + 2) > pc)
            break;
        line = vm_word(slot);
    }
    return line;
}

int ib_execute(struct ib_program *program)
{
    uint8_t *base = (uint8_t *)mem;
    const uint8_t *ip = base + program->start, *end = base + program->end;
    ib_value *sp = bas_work.ib.stack;
    struct ib_loop *loops = bas_work.ib.loops;
    unsigned ret_sp = 0;
    unsigned op, arg, p, n;
    int status = IB_OK, result;
    ib_value *var;
#ifdef STBASIC_VM_ASM
    struct ib_cursor cursor;
    cursor.budget = 64;
#else
    unsigned budget = 64;
#endif

    for (p = program->data; p < MEMSIZE; ++p) {
        mem[p] = 0;
        if (!(p & 63))
            byield();
    }
    /* ip points at the current opcode until it succeeds, including on errors
       in multi-byte instructions. Branch targets in the bytecode are offsets. */
    if (er || stb_done) goto done;
    while (ip < end) {
#ifdef STBASIC_VM_ASM
        /* The compiler appends END and fixes every branch to an instruction
           boundary. The native loop returns on a C opcode, error or stop. */
        cursor.ip = ip;
        cursor.sp = sp;
        cursor.loops = loops;
        result = ib_dispatch(&cursor);
        ip = cursor.ip;
        sp = cursor.sp;
        loops = cursor.loops;
        if (result < 0) { status = -result; goto done; }
        if (result == IB_OP_COUNT) goto done;
        op = (unsigned)result;
#else
        if (!--budget) {
            byield();
            if (er || stb_done) goto done;
            budget = 64;
        }
        op = *ip;
#endif
        switch (op) {
        case IB_END:
            return IB_OK;
#ifndef STBASIC_VM_ASM
        case IB_CONST: case IB_ILOAD:
#endif
        case IB_LITERAL: case IB_LOAD:
            if (sp == bas_work.ib.stack + STACKSIZE) {
                status = IB_STACK;
                goto done;
            }
            if (op == IB_ILOAD || op == IB_LOAD) {
                var = scalar_at(ip[1]);
                if (op == IB_ILOAD)
                    sp->integer = var->integer;
                else
                    sp->real = var->real;
                ip += 2;
            } else {
                arg = vm_word(ip + 1);
                if (op == IB_CONST)
                    sp->integer = (int16_t)arg;
                else
                    sp->real = getnumber(arg, memread2);
                ip += 3;
            }
            ++sp;
            continue;
#ifndef STBASIC_VM_ASM
        case IB_ISTORE:
#endif
        case IB_STORE:
            var = scalar_at(ip[1]);
            --sp;
            if (op == IB_ISTORE)
                var->integer = sp->integer;
            else
                var->real = sp->real;
            ip += 2;
            continue;
        case IB_INDEX: case IB_PUT: {
            unsigned v = ip[1] & 127;
            unsigned index;
            uint8_t *array;
            if (op == IB_PUT)
                --sp;
            index = (uint16_t)sp[-1].integer;
            if (!bas_work.ib.array[v].data || index >= bas_work.ib.array[v].count)
                goto range;
            array = ib_array_data(bas_work.ib.array[v].data);
            if (ip[1] & 128) {
                p = (unsigned)(array - base) + 4 * index;
                if (op == IB_INDEX)
                    sp[-1].real = getnumber(p, memread2);
                else
                    setnumber(p, memwrite2, sp->real);
            } else {
                uint8_t *element = array + 2 * index;
                if (op == IB_INDEX)
                    sp[-1].integer = (int16_t)vm_word(element);
                else
                    vm_write_word(element, (uint16_t)sp->integer);
            }
            if (op == IB_PUT)
                --sp;
            ip += 2;
            continue;
        }
#ifndef STBASIC_VM_ASM
        case IB_PUT_CONST: case IB_COPY_VAR: {
            struct ib_array *destination;
            unsigned index, value;
            if (op == IB_PUT_CONST) {
                value = vm_word(ip + 1);
                index = (uint16_t)scalar_at(ip[3])->integer;
                destination = &bas_work.ib.array[ip[4] / sizeof(struct ib_array)];
            } else {
                struct ib_array *source = &bas_work.ib.array[ip[2] / sizeof(struct ib_array)];
                index = (uint16_t)scalar_at(ip[1])->integer;
                if (index >= source->count) goto range;
                value = vm_word(ib_array_data(source->data) + 2 * index);
                destination = &bas_work.ib.array[ip[3] / sizeof(struct ib_array)];
            }
            if (index >= destination->count) goto range;
            vm_write_word(ib_array_data(destination->data) + 2 * index, value);
            ip += op == IB_PUT_CONST ? 5 : 4;
            continue;
        }
        case IB_INDEX_VAR: case IB_INDEX_OFF: case IB_PUT_VAR: {
            uint16_t index = (uint16_t)scalar_at(ip[1])->integer;
            unsigned v = ip[2] / sizeof(struct ib_array);
            uint8_t *element;
            if (op != IB_PUT_VAR && sp == bas_work.ib.stack + STACKSIZE) {
                status = IB_STACK;
                goto done;
            }
            if (op == IB_INDEX_OFF) {
                uint16_t delta = (uint16_t)(int8_t)ip[3];
                /* Two negative operands can wrap to a positive subscript. */
                if (index & delta & 0x8000u) goto range;
                index = (uint16_t)(index + delta);
            }
            if (index >= bas_work.ib.array[v].count)
                goto range;
            element = ib_array_data(bas_work.ib.array[v].data) + 2u * index;
            if (op == IB_PUT_VAR) {
                --sp;
                vm_write_word(element, (uint16_t)sp->integer);
            } else {
                sp->integer = (int16_t)vm_word(element);
                ++sp;
            }
            ip += op == IB_INDEX_OFF ? 4 : 3;
            continue;
        }
        case IB_IADD:
            --sp;
            if (!ib_add16(&sp[-1].integer, sp->integer))
                goto range;
            break;
        case IB_ISUB:
            --sp;
            if (!ib_sub16(&sp[-1].integer, sp->integer))
                goto range;
            break;
#endif
        case IB_IMUL:
            --sp;
            if (!ib_mul16(&sp[-1].integer, sp->integer))
                goto range;
            break;
        case IB_MOD:
            --sp;
            if (!sp->integer) {
                status = IB_DIVZERO;
                goto done;
            }
            sp[-1].integer = sp->integer == -1 ? 0 : sp[-1].integer % sp->integer;
            break;
        case IB_INEG:
            if (sp[-1].integer == -32768)
                goto range;
            sp[-1].integer = -sp[-1].integer;
            break;
        case IB_NOT: sp[-1].integer = ~sp[-1].integer; break;
        case IB_AND: sp[-2].integer &= sp[-1].integer; --sp; break;
        case IB_OR:  sp[-2].integer |= sp[-1].integer; --sp; break;
        case IB_IEQ: case IB_INE: case IB_ILT: case IB_ILE: case IB_IGT: case IB_IGE:
            sp[-2].integer = -integer_compare(op, sp[-2].integer, sp[-1].integer);
            --sp;
            break;
        case IB_FADD: case IB_FSUB: case IB_FMUL: case IB_FDIV:
        case IB_FEQ: case IB_FNE: case IB_FLT: case IB_FLE: case IB_FGT: case IB_FGE:
            --sp;
            status = ib_float(op, sp - 1, sp);
            if (status || er || stb_done) goto done;
            break;
        case IB_FTOP: case IB_FLEFT: case IB_ITOP: case IB_ILEFT: case IB_FNEG:
        case IB_FSQR: case IB_FSIN: case IB_FCOS: case IB_FTAN:
            status = ib_float(op, sp - 1, sp - 1);
            if (status || er || stb_done) goto done;
            break;
#ifndef STBASIC_VM_ASM
        case IB_JVAR:
            if (matches(ip[4], scalar_at(ip[1])->integer, (int16_t)vm_word(ip + 2)))
                ip += 7;
            else
                ip = base + vm_word(ip + 5);
            continue;
        case IB_JCMP:
            sp -= 2;
            ip = matches(ip[1], sp->integer, sp[1].integer) ? ip + 4 : base + vm_word(ip + 2);
            continue;
#endif
        case IB_JZERO: case IB_JNZ:
            arg = ip[1];
            --sp;
            if (arg == IB_INT) {
                result = sp->integer == 0;
            } else {
                result = ib_float(IB_JZERO, sp, sp);
                if (er || stb_done) goto done;
            }
            if (op == IB_JNZ) result = !result;
            ip = result ? base + vm_word(ip + 2) : ip + 4;
            continue;
        case IB_CALL:
            if (ret_sp == GOSUBDEPTH) {
                status = IB_STACK;
                goto done;
            }
            bas_work.ib.returns[ret_sp++] = (unsigned)(ip - base) + 3;
            /* fall through */
        case IB_JUMP:
            ip = base + vm_word(ip + 1);
            continue;
        case IB_RETURN:
            if (!ret_sp) goto bad_loop;
            ip = base + bas_work.ib.returns[--ret_sp];
            continue;
        case IB_FOR:
            if (loops == bas_work.ib.loops + FORDEPTH) goto bad_loop;
            arg = ip[1];
            var = scalar_at(arg & 127);
            loops->variable = arg;
            loops->start = (unsigned)(ip - base) + 4;
            if (arg & 128) {
                loops->step.real = sp[-1].real;
                loops->limit.real = sp[-2].real;
                var->real = sp[-3].real;
            } else {
                loops->step.integer = sp[-1].integer;
                loops->limit.integer = sp[-2].integer;
                var->integer = sp[-3].integer;
            }
            sp -= 3;
            result = loop_in_range(loops, var, 0);
            if ((arg & 128) && (er || stb_done)) goto done;
            if (result < 0) goto range;
            if (result) {
                ++loops;
                ip += 4;
            } else
                ip = base + vm_word(ip + 2);
            continue;
        case IB_NEXT:
            arg = ip[1];
            if (loops == bas_work.ib.loops || loops[-1].variable != arg) goto bad_loop;
            result = loop_in_range(loops - 1, scalar_at(arg & 127), 1);
            if ((arg & 128) && (er || stb_done)) goto done;
            if (result < 0) goto range;
            if (result)
                ip = base + loops[-1].start;
            else {
                --loops;
                ip += 2;
            }
            continue;
        case IB_TEXT:
            p = vm_word(ip + 1);
            n = base[p++];
            while (n--)
                outch((char)base[p++]);
            if (er || stb_done) goto done;
            ip += 3;
            continue;
        case IB_PRINT:
            ib_print(ip[1], --sp);
            if (er || stb_done) goto done;
            ip += 2;
            continue;
        case IB_SPACE: case IB_NEWLINE:
            outch(op == IB_SPACE ? ' ' : '\n');
            if (er || stb_done) goto done;
            break;
        case IB_INPUT:
            arg = ip[1];
            status = ib_input(arg & 128, &bas_work.ib.scalar[arg & 127]);
            if (status || er || stb_done) goto done;
            ip += 2;
            continue;
        default:
            status = IB_SYNTAX;
            goto done;
        }
        ++ip;
    }
done:
    program->line = error_line(program, (unsigned)(ip - base));
    return status;
range:
    status = IB_RANGE;
    goto done;
bad_loop:
    status = IB_LOOP;
    goto done;
}
