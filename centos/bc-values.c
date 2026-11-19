#include "bytecode.h"

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
        if (!(a->real > -32769.0f && a->real < 32768.0f))
            return IB_RANGE;
        a->integer = (int16_t)a->real;
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
