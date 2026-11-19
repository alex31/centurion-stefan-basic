#include "bytecode.h"
#include "work.h"

static const char *const ib_errors[] = {
    "", "Unsupported statement or syntax", "Program, bytecode and arrays exceed BASIC RAM",
    "Invalid type or undeclared array", "Integer or array index out of range",
    "Unknown branch line", "Unmatched FOR/NEXT or RETURN", "Bytecode stack limit",
    "Division by zero"
};

int bas_is_typed(void)
{
    unsigned p = 0;
    while (p < top && mem[p] == LINENUMBER) {
        p += 3;
        if (p < top && (mem[p] == TDEFINT || mem[p] == TDIM))
            return 1;
        if (p < top && mem[p] != LINENUMBER && mem[p] != TREM)
            break;
        while (p < top && mem[p] != LINENUMBER)
            ++p; /* REM body contains only source characters */
    }
    return 0;
}

void bas_run_typed(unsigned line)
{
    struct ib_program program;
    int status = ib_compile(&program, line);
    /* A missing overlay reports EFILE and returns a default result. Its
       compiler may have left the program descriptor uninitialized. */
    if (er)
        return;
    if (!status)
        status = ib_execute(&program);
    if (status) {
        outsc("Bytecode error");
        if (program.line) {
            outsc(" at ");
            outnumber(program.line);
        }
        outsc(": ");
        outsc(ib_errors[status]);
        outcr();
    }
}

void ib_take(struct ib_compile *c)
{
    c->source = c->next;
    if (c->source >= top) {
        c->token = EOL;
        return;
    }
    c->token = mem[c->source];
    c->next = bas_next(c->source);
    if (!c->next) {
        c->error = IB_SYNTAX;
        c->token = EOL;
        c->next = top;
    }
    if (!(++c->scan_tick & 31))
        byield();
}

void ib_emit(struct ib_compile *c, unsigned value)
{
    if (c->out >= c->program.data) {
        c->error = IB_MEMORY;
        return;
    }
    mem[c->out++] = (mem_t)value;
}

void ib_word(struct ib_compile *c, unsigned value)
{
    ib_emit(c, value >> 8);
    ib_emit(c, value);
}

unsigned ib_variable(struct ib_compile *c)
{
    unsigned ch;
    if (c->token != VARIABLE && c->token != ARRAYVAR)
        goto invalid;
    ch = (uint8_t)mem[c->source + 1];
    if (ch >= 'A' && ch <= 'Z' && !mem[c->source + 2])
        return ch - 'A';
invalid:
    c->error = IB_TYPE;
    return 0;
}
