#include "bytecode.h"

static const char *const ib_errors[] = {
    "", "Unsupported statement or syntax", "BASIC memory exhausted",
    "Invalid type or undeclared array", "Number, array index or string length out of range",
    "Unknown branch line", "Unmatched FOR/NEXT or RETURN", "Bytecode stack limit",
    "Division by zero"
};

struct ib_state ib_state;

void ib_clear_arrays(void)
{
    unsigned i;
    for (i = 0; i < 26; ++i) {
        ib_state.array[i].data = 0;
        ib_state.array[i].count = 0;
    }
    himem = MEMSIZE;
}

void ib_reset(void)
{
    unsigned i;
    ib_clear_arrays();
    for (i = 0; i < 26; ++i) {
        ib_state.scalar[i].real = 0;
        ib_state.type[i] = IB_REAL;
    }
    ib_state.integer_literals = 0;
    ib_strings_reset();
}

static void run_program(struct ib_program *program, int status)
{
    /* A missing overlay reports EFILE before returning its default value. */
    if (er)
        return;
    if (!status)
        status = ib_execute(program);
    if (status) {
        outsc("Bytecode error");
        if (program->line) {
            outsc(" at ");
            outnumber(program->line);
        }
        outsc(": ");
        outsc(ib_errors[status]);
        outcr();
    }
}

void bas_run_program(unsigned line)
{
    struct ib_program program;
    int status;
    if (!top)
        return;
    ib_reset();
    status = ib_compile(&program, line);
    if (!status && !er)
        himem = program.data;
    else
        ib_clear_arrays();
    run_program(&program, status);
}

void bas_run_immediate(char *text)
{
    struct ib_program program;
    unsigned source = top;
    int length = bas_stage_immediate(text);
    int status = IB_MEMORY;
    if (length <= 0)
        return;
    program.line = 0;
    if ((unsigned)length < himem - source) {
        bas_copy(source, (unsigned)length);
        top = source + (unsigned)length;
        status = ib_compile_immediate(&program, source, himem);
        top = source;
    }
    run_program(&program, status);
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
    if (c->token != VARIABLE && c->token != ARRAYVAR && c->token != STRINGVAR)
        goto invalid;
    ch = (uint8_t)mem[c->source + 1];
    if (ch >= 'A' && ch <= 'Z' && !mem[c->source + 2])
        return ch - 'A';
invalid:
    c->error = IB_TYPE;
    return 0;
}
