#include "bytecode.h"

void ib_coerce(struct ib_compile *c, unsigned from, unsigned to)
{
    if (from == to || c->error)
        return;
    if (from == IB_STRING || to == IB_STRING) {
        c->error = IB_TYPE;
        return;
    }
    ib_emit(c, to == IB_INT ? IB_ITOP : IB_FTOP);
}

static unsigned precedence(token_t token)
{
    switch (token) {
    case TOR: return 1;
    case TAND: return 2;
    case '=': case NOTEQUAL: case '<': case LESSEREQUAL:
    case '>': case GREATEREQUAL: return 3;
    case '+': case '-': return 4;
    case '*': case '/': case '%': case TMOD: return 5;
    case '^': return 6;
    default: return 0;
    }
}

unsigned ib_expr_at(struct ib_compile *, unsigned);

/* The subexpression starts at a known instruction boundary. Fuse only a
   complete scalar read or scalar +/- small literal, with an integer array. */
static void emit_index(struct ib_compile *c, unsigned start, unsigned variable, unsigned type)
{
    unsigned length = c->out - start;
    if (!c->error && type == IB_INT && length >= 2 && mem[start] == IB_ILOAD) {
        /* Keep the scalar displacement already at start+1. */
        if (length == 2) {
            mem[start] = IB_INDEX_VAR;
            ib_emit(c, IB_ARRAY_OFFSET(variable));
            return;
        }
        if (length == 6 && mem[start + 2] == IB_CONST &&
            (mem[start + 5] == IB_IADD || mem[start + 5] == IB_ISUB)) {
            unsigned delta = IB_WORD(start + 3);
            if (delta <= 127) {
                if (mem[start + 5] == IB_ISUB) delta = 0u - delta;
                mem[start] = IB_INDEX_OFF;
                mem[start + 2] = (mem_t)IB_ARRAY_OFFSET(variable);
                mem[start + 3] = (mem_t)delta;
                c->out = start + 4;
                return;
            }
        }
    }
    ib_emit(c, IB_INDEX);
    ib_emit(c, variable | (type == IB_REAL ? 128 : 0));
}

static unsigned real_function(token_t token)
{
    switch (token) {
    case TSQR: return IB_FSQR;
    case TSIN: return IB_FSIN;
    case TCOS: return IB_FCOS;
    case TTAN: return IB_FTAN;
    case TATAN: return IB_FATAN;
    case TABS: return IB_FABS;
    case TINT: return IB_FINT;
    case TSGN: return IB_FSGN;
    case TLOG: return IB_FLOG;
    case TEXP: return IB_FEXP;
    case TRND: return IB_FRND;
    default: return 0;
    }
}

static unsigned string_function(struct ib_compile *c, token_t token)
{
    unsigned type, op, result = IB_STRING;
    ib_take(c);
    if (c->token != '(')
        goto syntax;
    ib_take(c);
    type = ib_expr_at(c, 1);
    if (token == TCHR || token == TSTR) {
        ib_coerce(c, type, token == TCHR ? IB_INT : IB_REAL);
        op = token == TCHR ? IB_SCHR : IB_SSTR;
    } else {
        ib_coerce(c, type, IB_STRING);
        switch (token) {
        case TLEN: op = IB_SLEN; result = IB_INT; break;
        case TASC: op = IB_SASC; result = IB_INT; break;
        case TVAL: op = IB_SVAL; result = IB_REAL; break;
        default:
            if (c->token != ',') goto syntax;
            ib_take(c);
            ib_coerce(c, ib_expr_at(c, 1), IB_INT);
            op = token == TLEFT ? IB_SLEFT : token == TRIGHT ? IB_SRIGHT : IB_SMID;
            if (token == TMID) {
                if (c->token == ',') {
                    ib_take(c);
                    ib_coerce(c, ib_expr_at(c, 1), IB_INT);
                } else {
                    ib_emit(c, IB_CONST);
                    ib_word(c, 255);
                }
            }
            break;
        }
    }
    if (c->token != ')') goto syntax;
    ib_take(c);
    ib_emit(c, op);
    return result;
syntax:
    if (!c->error) c->error = IB_SYNTAX;
    return IB_BAD;
}

unsigned ib_primary(struct ib_compile *c)
{
    unsigned type, variable;
    token_t t = c->token;
    if (t == STRING) {
        ib_emit(c, IB_SLITERAL);
        ib_word(c, c->source + 1);
        ib_take(c);
        return IB_STRING;
    }
    if (t == STRINGVAR) {
        variable = ib_variable(c);
        ib_take(c);
        ib_emit(c, IB_SLOAD);
        ib_emit(c, variable);
        return IB_STRING;
    }
    switch (t) {
    case TLEN: case TASC: case TCHR: case TSTR:
    case TVAL: case TLEFT: case TRIGHT: case TMID:
        return string_function(c, t);
    default: break;
    }
    if (t == NUMBER) {
        number_t value = getnumber(c->source + 1, memread2);
        if (ib_state.integer_literals && value >= -32768.0f && value <= 32767.0f && value == (int16_t)value) {
            ib_emit(c, IB_CONST);
            ib_word(c, (uint16_t)(int16_t)value);
            type = IB_INT;
        } else {
            ib_emit(c, IB_LITERAL);
            ib_word(c, c->source + 1);
            type = IB_REAL;
        }
        ib_take(c);
        return type;
    }
    if (t == VARIABLE || t == ARRAYVAR) {
        variable = ib_variable(c);
        type = ib_state.type[variable];
        ib_take(c);
        if (t == ARRAYVAR) {
            unsigned start;
            if (!ib_state.array[variable].data) {
                c->error = IB_TYPE;
                return IB_BAD;
            }
            if (c->token != '(')
                goto syntax;
            ib_take(c);
            start = c->out;
            ib_coerce(c, ib_expr_at(c, 1), IB_INT);
            if (c->token != ')')
                goto syntax;
            ib_take(c);
            emit_index(c, start, variable, type);
        } else {
            ib_emit(c, type == IB_INT ? IB_ILOAD : IB_LOAD);
            ib_emit(c, IB_SCALAR_OFFSET(variable));
        }
        return type;
    }
    if (t == TPOW) {
        ib_take(c);
        if (c->token != '(') goto syntax;
        ib_take(c);
        type = ib_expr_at(c, 1);
        ib_coerce(c, type, IB_REAL);
        if (c->token != ',') goto syntax;
        ib_take(c);
        type = ib_expr_at(c, 1);
        ib_coerce(c, type, IB_REAL);
        if (c->token != ')') goto syntax;
        ib_take(c);
        ib_emit(c, IB_FPOW);
        return IB_REAL;
    }
    variable = real_function(t);
    if (t == '(' || variable) {
        if (t != '(') {
            ib_take(c);
            if (c->token != '(')
                goto syntax;
        }
        ib_take(c);
        type = ib_expr_at(c, 1);
        if (c->token != ')')
            goto syntax;
        ib_take(c);
        if (t != '(') {
            ib_coerce(c, type, IB_REAL);
            ib_emit(c, variable);
            type = IB_REAL;
        }
        return type;
    }
    if (t == '+' || t == '-' || t == TNOT) {
        ib_take(c);
        type = ib_expr_at(c, t == TNOT ? 3 : 6);
        if (type == IB_STRING) {
            c->error = IB_TYPE;
            return IB_BAD;
        }
        if (t == '-')
            ib_emit(c, type == IB_INT ? IB_INEG : IB_FNEG);
        if (t == TNOT) {
            ib_coerce(c, type, IB_INT);
            ib_emit(c, IB_NOT);
            type = IB_INT;
        }
        return type;
    }
syntax:
    if (!c->error)
        c->error = IB_SYNTAX;
    return IB_BAD;
}

static unsigned binary(struct ib_compile *c, token_t t, unsigned a, unsigned b)
{
    unsigned op;
    unsigned real = a == IB_REAL || b == IB_REAL || t == '^';
    if (a == IB_STRING || b == IB_STRING) {
        c->comparison = 0;
        if (a != b) goto string_type;
        if (t == '+') {
            ib_emit(c, IB_SCAT);
            return IB_STRING;
        }
        switch (t) {
        case '=': op = IB_IEQ; break;
        case NOTEQUAL: op = IB_INE; break;
        case '<': op = IB_ILT; break;
        case LESSEREQUAL: op = IB_ILE; break;
        case '>': op = IB_IGT; break;
        case GREATEREQUAL: op = IB_IGE; break;
        default: goto string_type;
        }
        ib_emit(c, IB_SCMP);
        ib_emit(c, op);
        return IB_INT;
    }
    if (((t == TMOD || t == '%') && !real) || t == TAND || t == TOR) {
        /* Explicit integer coercion on both operands. */
        if (a == IB_REAL) {
            ib_emit(c, IB_ILEFT);
        }
        ib_coerce(c, b, IB_INT);
        ib_emit(c, t == TAND ? IB_AND : t == TOR ? IB_OR : IB_MOD);
        c->comparison = 0;
        return IB_INT;
    }
    if (real) {
        if (a == IB_INT)
            ib_emit(c, IB_FLEFT);
        ib_coerce(c, b, IB_REAL);
    }
    switch (t) {
    case '+': op = IB_IADD; break;
    case '-': op = IB_ISUB; break;
    case '*': op = IB_IMUL; break;
    case '/': op = real ? IB_FDIV : IB_IDIV; break;
    case '^': op = IB_FPOW; break;
    case TMOD: case '%': op = IB_FMOD; break;
    case '=': op = IB_IEQ; break;
    case NOTEQUAL: op = IB_INE; break;
    case '<': op = IB_ILT; break;
    case LESSEREQUAL: op = IB_ILE; break;
    case '>': op = IB_IGT; break;
    default: op = IB_IGE; break;
    }
    if (real && op <= IB_IMUL)
        op += IB_FADD - IB_IADD;
    else if (real && op >= IB_IEQ && op <= IB_IGE)
        op += IB_FEQ - IB_IEQ;
    ib_emit(c, op);
    c->comparison = op >= IB_IEQ && op <= IB_IGE ? op : 0;
    return (op >= IB_FADD && op <= IB_FDIV) || op == IB_FPOW || op == IB_FMOD ? IB_REAL : IB_INT;
string_type:
    c->error = IB_TYPE;
    return IB_BAD;
}

unsigned ib_expr_at(struct ib_compile *c, unsigned minimum)
{
    unsigned type, rhs, priority;
    token_t t;
    if (c->depth == 10) {
        c->error = IB_STACK;
        return IB_BAD;
    }
    ++c->depth;
    type = ib_primary(c);
    c->comparison = 0;
    while (!c->error && (priority = precedence(c->token)) >= minimum) {
        t = c->token;
        ib_take(c);
        rhs = ib_expr_at(c, priority + (t != '^'));
        if (!c->error)
            type = binary(c, t, type, rhs);
    }
    --c->depth;
    return type;
}

unsigned ib_expr(struct ib_compile *c)
{
    return ib_expr_at(c, 1);
}
