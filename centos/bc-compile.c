#include "bytecode.h"

static int eat(struct ib_compile *c, token_t token)
{
    if (c->token != token) {
        if (!c->error)
            c->error = IB_SYNTAX;
        return 0;
    }
    ib_take(c);
    return 1;
}

static int end_statement(const struct ib_compile *c)
{
    return c->token == EOL || c->token == LINENUMBER || c->token == ':';
}

static unsigned literal(struct ib_compile *c, unsigned limit)
{
    number_t n;
    if (c->token != NUMBER) {
        c->error = IB_SYNTAX;
        return 0;
    }
    n = getnumber(c->source + 1, memread2);
    ib_take(c);
    if (!(n >= 0.0f && n <= (number_t)limit) || n != (unsigned)n) {
        c->error = IB_RANGE;
        return 0;
    }
    return (unsigned)n;
}

static unsigned branch_slot(struct ib_compile *c, unsigned line)
{
    unsigned lo = 0, hi = c->program.lines;
    unsigned p, value;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        p = c->program.table + 4 * mid;
        value = IB_WORD(p);
        if (value == line)
            return p + 2;
        if (value < line)
            lo = mid + 1;
        else
            hi = mid;
    }
    c->error = IB_BRANCH;
    return 0;
}

static void branch_target(struct ib_compile *c, unsigned line)
{
    unsigned slot = branch_slot(c, line), target, operand;
    if (!slot)
        return;
    target = IB_WORD(slot);
    operand = c->out;
    ib_word(c, target & 0x7fffu);
    /* An unresolved line points to a chain of operands to be patched. */
    if (!c->error && (!target || (target & 0x8000u)))
        IB_SET(slot, operand | 0x8000u);
}

void ib_declare_int(struct ib_compile *c)
{
    unsigned low, high, v;
    do {
        if (c->token != VARIABLE) {
            c->error = IB_SYNTAX;
            return;
        }
        low = high = ib_variable(c);
        ib_take(c);
        if (c->token == '-') {
            ib_take(c);
            if (c->token != VARIABLE) {
                c->error = IB_SYNTAX;
                return;
            }
            high = ib_variable(c);
            ib_take(c);
        }
        if (high < low) {
            c->error = IB_RANGE;
            return;
        }
        ib_state.integer_literals = 1;
        for (v = low; v <= high; ++v)
            ib_state.type[v] = IB_INT;
        if (c->token != ',')
            return;
        ib_take(c);
    } while (!c->error);
}

void ib_dimension(struct ib_compile *c)
{
    unsigned v, bound, bytes, width;
    do {
        if (c->token != ARRAYVAR) {
            c->error = IB_SYNTAX;
            return;
        }
        v = ib_variable(c);
        ib_take(c);
        if (!eat(c, '('))
            return;
        bound = literal(c, MEMSIZE / 2 - 1);
        if (!eat(c, ')') || c->error)
            return;
        if (ib_state.array[v].data) {
            c->error = IB_TYPE;
            return;
        }
        width = ib_state.type[v] == IB_INT ? 2 : 4;
        bytes = width * (bound + 1);
        if (bytes > c->program.data - c->out) {
            c->error = IB_MEMORY;
            return;
        }
        c->program.data -= bytes;
        ib_state.array[v].data = ib_array_at(c->program.data);
        ib_state.array[v].count = bound + 1;
        if (c->token != ',')
            return;
        ib_take(c);
    } while (!c->error);
}

void ib_assign(struct ib_compile *c)
{
    unsigned array = c->token == ARRAYVAR, string = c->token == STRINGVAR;
    unsigned v = ib_variable(c);
    unsigned t = string ? IB_STRING : ib_state.type[v];
    unsigned start, index_var = 255;
    ib_take(c);
    if (array) {
        if (!ib_state.array[v].data) {
            c->error = IB_TYPE;
            return;
        }
        if (!eat(c, '('))
            return;
        start = c->out;
        ib_coerce(c, ib_expr(c), IB_INT);
        if (!c->error && t == IB_INT && c->out - start == 2 && mem[start] == IB_ILOAD) {
            /* Bytecode expressions cannot assign a scalar while evaluating
               the right side. Check the subscript at the original store. */
            index_var = (uint8_t)mem[start + 1];
            c->out = start;
        }
        if (!eat(c, ')'))
            return;
    }
    if (!eat(c, '='))
        return;
    start = c->out;
    ib_coerce(c, ib_expr(c), t);
    if (string && !c->error && c->out - start == 6 &&
        mem[start] == IB_SLOAD && (unsigned char)mem[start + 1] == v &&
        mem[start + 2] == IB_SLITERAL && mem[start + 5] == IB_SCAT) {
        /* A$=A$+literal needs neither a stack value nor a temporary string. */
        mem[start] = IB_SAPPEND;
        mem[start + 2] = mem[start + 3];
        mem[start + 3] = mem[start + 4];
        c->out = start + 4;
        return;
    }
    if (index_var != 255) {
        if (!c->error && c->out - start == 3 && mem[start] == IB_CONST) {
            mem[start] = IB_PUT_CONST;  /* retain the immediate word */
            ib_emit(c, index_var);
        } else if (!c->error && c->out - start == 3 && mem[start] == IB_INDEX_VAR &&
                   (uint8_t)mem[start + 1] == index_var) {
            mem[start] = IB_COPY_VAR;   /* retain the scalar and source array */
        } else {
            ib_emit(c, IB_PUT_VAR);
            ib_emit(c, index_var);
        }
        ib_emit(c, IB_ARRAY_OFFSET(v));
        return;
    }
    ib_emit(c, array ? IB_PUT : string ? IB_SSTORE : t == IB_INT ? IB_ISTORE : IB_STORE);
    ib_emit(c, array ? v | (t == IB_REAL ? 128 : 0) : string ? v : IB_SCALAR_OFFSET(v));
}

void ib_print_stmt(struct ib_compile *c)
{
    unsigned newline = 1, type;
    token_t following;
    while (!c->error && !end_statement(c)) {
        following = c->next < top ? mem[c->next] : EOL;
        if (c->token == STRING &&
            (following == EOL || following == LINENUMBER || following == ':' ||
             following == ';' || following == ',')) {
            ib_emit(c, IB_TEXT);
            ib_word(c, c->source + 1);
            ib_take(c);
        } else {
            type = ib_expr(c);
            ib_emit(c, type == IB_STRING ? IB_SPRINT : IB_PRINT);
            if (type != IB_STRING)
                ib_emit(c, type);
        }
        newline = c->token != ';' && c->token != ',';
        if (newline)
            break;
        if (c->token == ',')
            ib_emit(c, IB_SPACE);
        ib_take(c);
    }
    if (newline)
        ib_emit(c, IB_NEWLINE);
}

void ib_for_stmt(struct ib_compile *c)
{
    unsigned v, t, depth = c->loops;
    if (depth == FORDEPTH) {
        c->error = IB_LOOP;
        return;
    }
    if (c->token != VARIABLE) {
        c->error = IB_TYPE;
        return;
    }
    v = ib_variable(c);
    t = ib_state.type[v];
    ib_take(c);
    if (!eat(c, '='))
        return;
    ib_coerce(c, ib_expr(c), t);
    if (!eat(c, TTO))
        return;
    ib_coerce(c, ib_expr(c), t);
    if (c->token == TSTEP) {
        ib_take(c);
        ib_coerce(c, ib_expr(c), t);
    } else {
        ib_emit(c, IB_CONST);
        ib_word(c, 1);
        ib_coerce(c, IB_INT, t);
    }
    ib_emit(c, IB_FOR);
    c->variable[depth] = IB_SCALAR_OFFSET(v) | (t == IB_REAL ? 128 : 0);
    ib_emit(c, c->variable[depth]);
    c->exit_patch[depth] = c->out;
    ib_word(c, 0);
    ++c->loops;
}

void ib_next_stmt(struct ib_compile *c)
{
    unsigned depth;
    if (!c->loops) {
        c->error = IB_LOOP;
        return;
    }
    depth = --c->loops;
    if (c->token == VARIABLE) {
        if ((c->variable[depth] & 127) != IB_SCALAR_OFFSET(ib_variable(c))) {
            c->error = IB_LOOP;
            return;
        }
        ib_take(c);
    }
    ib_emit(c, IB_NEXT);
    ib_emit(c, c->variable[depth]);
    if (!c->error)
        IB_SET(c->exit_patch[depth], c->out);
}

void ib_stmt(struct ib_compile *c)
{
    unsigned start, patch, type, v, line;
    token_t token = c->token;
    if (token != VARIABLE && token != ARRAYVAR && token != STRINGVAR)
        ib_take(c);
    switch (token) {
    case TREM:
        while (c->token != LINENUMBER && c->token != EOL)
            ib_take(c);
        break;
    case TLET: case VARIABLE: case ARRAYVAR: case STRINGVAR:
        ib_assign(c);
        break;
    case TPRINT: ib_print_stmt(c); break;
    case TINPUT: {
        if (c->token != VARIABLE && c->token != STRINGVAR) {
            c->error = IB_TYPE;
            break;
        }
        v = ib_variable(c);
        if (c->token == STRINGVAR) {
            ib_emit(c, IB_SINPUT);
            ib_emit(c, v);
        } else {
            ib_emit(c, IB_INPUT);
            ib_emit(c, v | (ib_state.type[v] == IB_REAL ? 128 : 0));
        }
        ib_take(c);
        break;
    }
    case TGOTO: case TGOSUB:
        line = literal(c, 65535);
        if (!c->error) {
            ib_emit(c, token == TGOTO ? IB_JUMP : IB_CALL);
            branch_target(c, line);
        }
        break;
    case TIF: {
        /* One bit each for less, equal and greater. The VM jumps when the
           relation is absent from the mask. */
        static const uint8_t masks[] = {2, 5, 1, 3, 4, 6};
        unsigned numeric, direct, mask = 0;
        start = c->out;
        type = ib_expr(c);
        if (type == IB_STRING) {
            c->error = IB_TYPE;
            break;
        }
        if (!eat(c, TTHEN))
            break;
        numeric = c->token == NUMBER;
        if (numeric)
            line = literal(c, 65535);
        if (c->error) break;
        direct = numeric && (c->token == LINENUMBER || c->token == EOL);
        if (c->comparison) {
            mask = masks[c->comparison - IB_IEQ];
            if (direct) mask ^= 7;
        }
        if (mask && c->out - start == 6 &&
            mem[start] == IB_ILOAD && mem[start + 2] == IB_CONST) {
            /* The mask immediately precedes the target for both forms. */
            mem[start] = IB_JVAR;
            mem[start + 2] = mem[start + 3];
            mem[start + 3] = mem[start + 4];
            mem[start + 4] = (mem_t)mask;
            --c->out;
        } else {
            if (mask)
                --c->out;
            ib_emit(c, mask ? IB_JCMP : direct ? IB_JNZ : IB_JZERO);
            ib_emit(c, mask ? mask : type);
        }
        if (direct) {
            branch_target(c, line);
            break;
        }
        patch = c->out;
        ib_word(c, 0);
        if (numeric) {
            ib_emit(c, IB_JUMP);
            branch_target(c, line);
        } else {
            /* A false IF skips the rest of its physical line. Avoid nested
               statement recursion here to bound the target C stack. */
            if (c->token == TIF || c->token == TFOR || c->token == TNEXT ||
                c->token == TDEFINT || c->token == TDIM || end_statement(c)) {
                c->error = IB_SYNTAX;
                break;
            }
            ib_stmt(c);
        }
        while (!c->error && c->token == ':') {
            ib_take(c);
            if (c->token == TIF || c->token == TFOR || c->token == TNEXT ||
                c->token == TDEFINT || c->token == TDIM || end_statement(c)) {
                c->error = IB_SYNTAX;
                break;
            }
            ib_stmt(c);
        }
        if (!c->error)
            IB_SET(patch, c->out);
        break;
    }
    case TFOR: ib_for_stmt(c); break;
    case TNEXT: ib_next_stmt(c); break;
    case TRETURN: ib_emit(c, IB_RETURN); break;
    case TSTOP: case TEND: ib_emit(c, IB_END); break;
    default: c->error = IB_SYNTAX; break;
    }
}

/* Stay in the emission overlay while walking the entire source. */
void ib_lines(struct ib_compile *c)
{
    unsigned line, p, i, slot = c->program.table;
    while (!c->error && c->token == LINENUMBER) {
        line = bas_line(c->source);
        c->program.line = line;
        p = IB_WORD(slot + 2) & 0x7fffu;
        IB_SET(slot + 2, c->out);
        while (p) {
            i = IB_WORD(p);
            IB_SET(p, c->out);
            p = i;
        }
        slot += 4;
        ib_take(c);

        while (!c->error && c->token != LINENUMBER && c->token != EOL) {
            if (c->header == 2 && c->token == TDEFINT) {
                ib_take(c);
                ib_declare_int(c);
            } else if (c->header && c->token == TDIM) {
                c->header = 1;
                ib_take(c);
                ib_dimension(c);
            } else {
                if (c->token != TREM)
                    c->header = 0;
                ib_stmt(c);
            }
            if (c->error || c->token == LINENUMBER || c->token == EOL)
                break;
            eat(c, ':');
        }
    }
}

int ib_compile(struct ib_program *program, unsigned start_line)
{
    struct ib_compile c;
    unsigned p, i, slot;
    unsigned count = 0;
    program->line = 0;
    for (i = 0; i < sizeof c; ++i)
        ((uint8_t *)&c)[i] = 0;
    for (p = 0; p < top; ) {
        if (mem[p] == LINENUMBER)
            ++count;
        p = bas_next(p);
        if (!p)
            return IB_SYNTAX;
    }
    if (!count || count >= (MEMSIZE - top) / 4)
        return IB_MEMORY;

    c.program.table = top;
    c.program.lines = count;
    c.program.start = c.out = top + 4 * count;
    c.program.data = MEMSIZE;
    c.header = 2;

    for (p = 0, slot = top; p < top; p = bas_next(p)) {
        if (mem[p] == LINENUMBER) {
            IB_SET(slot, bas_line(p));
            IB_SET(slot + 2, 0);
            slot += 4;
        }
    }

    ib_take(&c);
    ib_lines(&c);
    if (!c.error && c.loops)
        c.error = IB_LOOP;
    if (!c.error && c.token != EOL)
        c.error = IB_SYNTAX;
    if (!c.error)
        ib_emit(&c, IB_END);
    if (!c.error && start_line) {
        p = branch_slot(&c, start_line);
        if (!c.error)
            c.program.start = IB_WORD(p);
    }
    c.program.end = c.out;
    *program = c.program;
    return c.error;
}


int ib_compile_immediate(struct ib_program *program, unsigned source, unsigned limit)
{
    struct ib_compile c;
    unsigned i;
    for (i = 0; i < sizeof c; ++i)
        ((uint8_t *)&c)[i] = 0;
    c.next = source;
    c.program.start = c.out = top;
    c.program.data = limit;
    ib_take(&c);
    while (!c.error && c.token != EOL) {
        ib_stmt(&c);
        if (!c.error && c.token != EOL)
            eat(&c, ':');
    }
    if (!c.error && c.loops)
        c.error = IB_LOOP;
    if (!c.error)
        ib_emit(&c, IB_END);
    c.program.end = c.out;
    *program = c.program;
    return c.error;
}
