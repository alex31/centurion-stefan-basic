#include "bytecode.h"

/* The cursor indexes the stored source, which survives bytecode recompilation. */
static uint16_t data_cursor;

void ib_reset_data(void)
{
    data_cursor = 0;
}

static int next_data(unsigned *cursor, unsigned *value, unsigned *negative)
{
    unsigned p = *cursor, next, remark = 0, scanned = 0;
    token_t token;

    if (p < top && mem[p] == ',') {
        ++p;
    } else {
        while (p < top) {
            token = mem[p];
            if (token == LINENUMBER)
                remark = 0;
            else if (token == TREM)
                remark = 1;
            else if (!remark && token == TDATA)
                break;
            next = bas_next(p);
            if (!next) return IB_SYNTAX;
            p = next;
            if (!(++scanned & 31)) byield();
        }
        if (p == top) return IB_OUT_OF_DATA;
        ++p;
    }
    if (p >= top) return IB_SYNTAX;
    token = mem[p];
    *negative = token == '-';
    if (token == '+' || token == '-') {
        ++p;
        if (p >= top || mem[p] != NUMBER) return IB_SYNTAX;
    } else if (token != NUMBER && token != STRING) {
        return IB_SYNTAX;
    }
    next = bas_next(p);
    if (!next || (next < top && mem[next] != ',' && mem[next] != ':' &&
                  mem[next] != LINENUMBER))
        return IB_SYNTAX;
    *value = p;
    *cursor = next;
    return IB_OK;
}

int ib_restore_data(unsigned item)
{
    unsigned cursor = 0, start = 0, value, negative;
    while (item--) {
        int status;
        start = cursor;
        status = next_data(&cursor, &value, &negative);
        if (status) return status;
        byield();
    }
    data_cursor = start;
    return IB_OK;
}

int ib_read_data(unsigned type, ib_value *value)
{
    unsigned cursor = data_cursor, p, negative;
    int status = next_data(&cursor, &p, &negative);
    if (status) return status;
    if (type == IB_STRING) {
        if (mem[p] != STRING) return IB_TYPE;
        value->string.length = (uint8_t)mem[p + 1];
        value->string.location = p + 2;
    } else {
        if (mem[p] != NUMBER) return IB_TYPE;
        value->real = getnumber(p + 1, memread2);
        if (negative) value->real = -value->real;
        if (type == IB_INT) {
            status = ib_float(IB_ITOP, value, value);
            if (status) return status;
        }
    }
    data_cursor = cursor;
    return IB_OK;
}
