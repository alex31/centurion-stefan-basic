#include "bytecode.h"
extern float sb_pow(float, float);
extern float sb_mod(float, float);
extern void xrnd(void);

int ib_float(unsigned op, ib_value *a, ib_value *b)
{
    number_t left, right;
    if (op == IB_FLEFT || op == IB_ILEFT)
        --a;
    if (op == IB_FTOP || op == IB_FLEFT) {
        a->real = (number_t)a->integer;
        return IB_OK;
    }
    if (op == IB_ITOP || op == IB_ILEFT) {
#ifdef __CPU6_OS6__
        unsigned high = a->words[0], low = a->words[1];
        unsigned magnitude = high & 0x7fffu;
        unsigned negative = high & 0x8000u;
        unsigned truncated = 0;
        if (magnitude >= 0x4700u) {
            /* A negative fraction below 32769 still truncates to -32768. */
            if (!negative || magnitude != 0x4700u || low >= 0x100u)
                return IB_RANGE;
            truncated = 0x8000u;
        } else if (magnitude >= 0x3f80u) {
            unsigned exponent = (magnitude >> 7) - 127u;
            unsigned significand = (magnitude & 127u) | 128u;
            if (exponent <= 7)
                truncated = significand >> (7 - exponent);
            else
                truncated = (significand << (exponent - 7)) | (low >> (23 - exponent));
        }
        a->integer = (int16_t)(negative ? 0u - truncated : truncated);
#else
        if (!(a->real > -32769.0f && a->real < 32768.0f))
            return IB_RANGE;
        a->integer = (int16_t)a->real;
#endif
        return IB_OK;
    }
    left = a->real;
    right = b->real;
    switch (op) {
    case IB_FADD: a->real = left + right; break;
    case IB_FSUB: a->real = left - right; break;
    case IB_FMUL: a->real = left * right; break;
    case IB_FDIV:
        if (right == 0.0f)
            return IB_DIVZERO;
        a->real = left / right;
        break;
    case IB_FPOW: a->real = sb_pow(left, right); break;
    case IB_FMOD: a->real = sb_mod(left, right); break;
    case IB_FABS: a->real = fabsf(left); break;
    case IB_FINT: a->real = floorf(left); break;
    case IB_FSGN: a->real = left < 0.0f ? -1.0f : left > 0.0f ? 1.0f : 0.0f; break;
    case IB_FATAN: a->real = atanf(left); break;
    case IB_FLOG: a->real = logf(left); break;
    case IB_FEXP: a->real = expf(left); break;
    case IB_FRND: push(left); xrnd(); a->real = pop(); break;
    case IB_FNEG: a->real = -left; break;
    case IB_FSQR: a->real = sqrtf(left); break;
    case IB_FSIN: a->real = sinf(left); break;
    case IB_FCOS: a->real = cosf(left); break;
    case IB_FTAN: a->real = tanf(left); break;
    case IB_FEQ: a->integer = left == right ? -1 : 0; break;
    case IB_FNE: a->integer = left != right ? -1 : 0; break;
    case IB_FLT: a->integer = left < right ? -1 : 0; break;
    case IB_FLE: a->integer = left <= right ? -1 : 0; break;
    case IB_FGT: a->integer = left > right ? -1 : 0; break;
    case IB_FGE: a->integer = left >= right ? -1 : 0; break;
    case IB_JZERO: return left == 0.0f;
    }
    return IB_OK;
}

int ib_for_real(struct ib_loop *loop, ib_value *variable, unsigned advance)
{
#ifdef __CPU6_OS6__
    if (advance && loop->step.bits == 0x3f800000UL && loop->limit.bits <= 0x7f800000UL) {
        static const uint32_t units[] = {
            0x800000UL, 0x400000UL, 0x200000UL, 0x100000UL, 0x80000UL, 0x40000UL,
            0x20000UL, 0x10000UL, 0x8000UL, 0x4000UL, 0x2000UL, 0x1000UL,
            0x800UL, 0x400UL, 0x200UL, 0x100UL, 0x80UL, 0x40UL,
            0x20UL, 0x10UL, 8UL, 4UL, 2UL, 1UL,
        };
        uint32_t bits = variable->bits;
        unsigned exponent = (unsigned)(bits >> 23) - 127;
        if (exponent < sizeof units / sizeof units[0]) {
            uint32_t unit = units[exponent];
            /* For whole binary32 values from 1 up to 2^24-1, adding one
               encoded unit is exact, including a carry to the next exponent. */
            if (!(bits & (unit - 1))) {
                variable->bits = bits + unit;
                return variable->bits <= loop->limit.bits;
            }
        }
    }
#endif
    number_t step = loop->step.real, value = variable->real, updated;
    if (!step)
        return -1;
    if (advance) {
        updated = value + step;
        if (updated == value)
            return -1;
        value = variable->real = updated;
    }
    return step > 0 ? value <= loop->limit.real : value >= loop->limit.real;
}

void ib_print(unsigned type, ib_value *value)
{
    char number[12];
    if (type == IB_INT) {
        writenumber(number, value->integer);
        outsc(number);
    } else {
        outnumber(value->real);
    }
}

int ib_input(unsigned wide, ib_value *value)
{
    char buffer[BUFSIZE];
    char *text;
    unsigned length, negative = 0;
    outsc("? ");
    ins(buffer, sizeof buffer);
    if (stb_done)
        return IB_OK;
    text = buffer + 1;
    while (*text == ' ' || *text == '\t')
        ++text;
    if (*text == '-' || *text == '+')
        negative = *text++ == '-';
    length = parsenumber2(text, &value->real);
    if (!length)
        return IB_SYNTAX;
    text += length;
    while (*text == ' ' || *text == '\t')
        ++text;
    if (*text)
        return IB_SYNTAX;
    if (negative)
        value->real = -value->real;
    return wide ? IB_OK : ib_float(IB_ITOP, value, value);
}
