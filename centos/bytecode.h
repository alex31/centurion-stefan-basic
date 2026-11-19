#ifndef BASIC_BYTECODE_H
#define BASIC_BYTECODE_H

#include "app.h"

enum ib_type { IB_BAD, IB_INT, IB_REAL };
enum ib_status { IB_OK, IB_SYNTAX, IB_MEMORY, IB_TYPE, IB_RANGE, IB_BRANCH,
                 IB_LOOP, IB_STACK, IB_DIVZERO };
enum ib_op {
#define IB_OP(name, handler) IB_##name,
#include "bc-ops.def"
#undef IB_OP
    IB_OP_COUNT
};

typedef union { int16_t integer; number_t real; } ib_value;
#define IB_SCALAR_OFFSET(v) ((v) * sizeof(ib_value))
struct ib_loop {
    ib_value limit, step;
    uint16_t start;
    uint8_t variable;
};
/* Target array entries are native addresses. On the host an arena offset
   keeps the same two-byte entry and the same 448-byte shared workspace. */
#ifdef __CPU6_OS6__
typedef uint8_t *ib_array_ref;
static inline ib_array_ref ib_array_at(unsigned offset) { return (uint8_t *)mem + offset; }
static inline uint8_t *ib_array_data(ib_array_ref ref) { return ref; }
#else
typedef uint16_t ib_array_ref;
static inline ib_array_ref ib_array_at(unsigned offset) { return (uint16_t)offset; }
static inline uint8_t *ib_array_data(ib_array_ref ref) { return (uint8_t *)mem + ref; }
#endif
struct ib_array { ib_array_ref data; uint16_t count; };
#define IB_ARRAY_OFFSET(v) ((v) * sizeof(struct ib_array))
struct ib_state {
    ib_value scalar[26], stack[STACKSIZE];
    struct ib_loop loops[FORDEPTH];
    struct ib_array array[26];
    uint16_t returns[GOSUBDEPTH];
    uint8_t type[26];
};

struct ib_program {
    unsigned table, lines, start, end, data;
    unsigned line;
};

struct ib_compile {
    struct ib_program program;
    unsigned source, next, out, header;
    unsigned loops, error;
    token_t token;
    uint8_t depth, scan_tick, comparison;
    uint8_t variable[FORDEPTH];
    unsigned exit_patch[FORDEPTH];
};

#define IB_WORD(p) (((unsigned)(uint8_t)mem[(p)] << 8) | (uint8_t)mem[(p) + 1])
#define IB_SET(p,v) do { mem[(p)] = (mem_t)((unsigned)(v) >> 8); mem[(p) + 1] = (mem_t)(v); } while (0)

int bas_is_typed(void);
void bas_run_typed(unsigned);
int ib_compile(struct ib_program *, unsigned);
int ib_execute(struct ib_program *);
void ib_take(struct ib_compile *);
void ib_emit(struct ib_compile *, unsigned);
void ib_word(struct ib_compile *, unsigned);
unsigned ib_variable(struct ib_compile *);
unsigned ib_expr(struct ib_compile *);
void ib_coerce(struct ib_compile *, unsigned, unsigned);
int ib_float(unsigned, ib_value *, ib_value *);
void ib_print(unsigned, ib_value *);
int ib_input(unsigned, ib_value *);
int ib_for_real(struct ib_loop *, ib_value *, unsigned);
#endif
