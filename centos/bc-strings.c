#include "bytecode.h"
#include "bc-string-layout.h"

#define STRING_TEMP 1024
#define OFFSET_MASK (IB_STRING_HEAP_REF - 1)
#define TEMP_REF 0x8000U

/* Program literals, permanent strings and expression temporaries are distinct.
   Only assignment compacts the permanent heap. A value never survives that
   operation on the expression stack. */
struct ib_string ib_string_vars[IB_STRING_VARIABLES];
char ib_string_heap[IB_STRING_HEAP_SIZE];
uint16_t ib_string_used;
static char temporary[STRING_TEMP], scratch[IB_STRING_LIMIT + 1];
static uint16_t temp_end;

#ifdef STBASIC_VM_ASM
typedef char ib_string_dispatch_layout[
    sizeof(struct ib_string) == 4 &&
    __builtin_offsetof(struct ib_string, location) == 0 &&
    __builtin_offsetof(struct ib_string, length) == 2 ? 1 : -1];
#endif

static const char *bytes(const struct ib_string *value)
{
    unsigned offset = value->location & OFFSET_MASK;
    if (value->location & TEMP_REF) return temporary + offset;
    if (value->location & IB_STRING_HEAP_REF) return ib_string_heap + offset;
    return (const char *)mem + offset;
}

static void release(const struct ib_string *value)
{
    unsigned offset = value->location & OFFSET_MASK;
    if ((value->location & TEMP_REF) && offset + value->length == temp_end)
        temp_end = offset;
}

static void copy(char *dst, const char *src, unsigned length)
{
    while (length--) *dst++ = *src++;
}

static int result_string(struct ib_string *value, unsigned length)
{
    if (length > STRING_TEMP - temp_end) return IB_MEMORY;
    value->location = TEMP_REF | temp_end;
    value->length = length;
    copy(temporary + temp_end, scratch, length);
    temp_end += length;
    return IB_OK;
}

static int replace(unsigned slot, unsigned length)
{
    struct ib_string *target = &ib_string_vars[slot];
    unsigned old = target->length, start = target->location & OFFSET_MASK;
    if (length > IB_STRING_HEAP_SIZE - (ib_string_used - old)) return IB_MEMORY;
    if (old && start + old != ib_string_used) {
        unsigned end = start + old;
        unsigned i;
        /* The destination is below the source, so the forward copy is safe. */
        copy(ib_string_heap + start, ib_string_heap + end, ib_string_used - end);
        for (i = 0; i < IB_STRING_VARIABLES; ++i)
            if (ib_string_vars[i].length && (ib_string_vars[i].location & OFFSET_MASK) >= end)
                ib_string_vars[i].location -= old;
    }
    ib_string_used -= old;
    target->location = IB_STRING_HEAP_REF | ib_string_used;
    target->length = length;
    copy(ib_string_heap + ib_string_used, scratch, length);
    ib_string_used += length;
    return IB_OK;
}

void ib_strings_reset(void)
{
    unsigned i;
    for (i = 0; i < IB_STRING_VARIABLES; ++i) ib_string_vars[i].length = 0;
    ib_string_used = temp_end = 0;
}

void ib_strings_begin(void) { temp_end = 0; }

void ib_string_load(unsigned slot, struct ib_string *value)
{
    *value = ib_string_vars[slot];
}

int ib_string_store(unsigned slot, const struct ib_string *value)
{
    unsigned length = value->length;
    copy(scratch, bytes(value), length);
    release(value);
    return replace(slot, length);
}

static int append(unsigned slot, const char *source, unsigned length)
{
    struct ib_string *target = &ib_string_vars[slot];
    unsigned old = target->length, start = target->location & OFFSET_MASK;
    if (old + length > IB_STRING_LIMIT) return IB_RANGE;
    if (length > IB_STRING_HEAP_SIZE - ib_string_used) return IB_MEMORY;
    if (!old || start + old == ib_string_used) {
        if (!old) target->location = IB_STRING_HEAP_REF | ib_string_used;
        copy(ib_string_heap + ib_string_used, source, length);
        ib_string_used += length;
        target->length += length;
        return IB_OK;
    }
    copy(scratch, ib_string_heap + start, old);
    copy(scratch + old, source, length);
    return replace(slot, old + length);
}

int ib_string_append_literal(unsigned slot, unsigned literal)
{
    return append(slot, (const char *)mem + literal + 1, (uint8_t)mem[literal]);
}

int ib_string_join(struct ib_string *left, const struct ib_string *right, unsigned slot)
{
    unsigned length = left->length + right->length;
    if (length > IB_STRING_LIMIT) return IB_RANGE;
    if (slot < IB_STRING_VARIABLES) {
        struct ib_string *target = &ib_string_vars[slot];
        if (left->length == target->length &&
            (!target->length || left->location == target->location)) {
            int status = append(slot, bytes(right), right->length);
            release(right);
            release(left);
            return status;
        }
    }
    copy(scratch, bytes(left), left->length);
    copy(scratch + left->length, bytes(right), right->length);
    release(right);
    release(left);
    return slot < IB_STRING_VARIABLES ? replace(slot, length) : result_string(left, length);
}

int ib_string_compare(const struct ib_string *left, const struct ib_string *right)
{
    const unsigned char *a = (const unsigned char *)bytes(left);
    const unsigned char *b = (const unsigned char *)bytes(right);
    unsigned i, n = left->length, m = right->length;
    int result = n < m ? -1 : n != m;
    for (i = 0; i < n && i < m; ++i) {
        if (a[i] != b[i]) {
            result = a[i] < b[i] ? -1 : 1;
            break;
        }
    }
    release(right);
    release(left);
    return result;
}

void ib_string_print(const struct ib_string *value)
{
    outs((char *)bytes(value), value->length);
    release(value);
}

int ib_string_input(unsigned slot)
{
    char buffer[BUFSIZE];
    unsigned length;
    outsc("? ");
    ins(buffer, sizeof buffer);
    if (stb_done) return IB_OK;
    length = (unsigned char)buffer[0];
    copy(scratch, buffer + 1, length);
    return replace(slot, length);
}

unsigned ib_string_length(const struct ib_string *value)
{
    unsigned length = value->length;
    release(value);
    return length;
}

int ib_string_asc(const struct ib_string *value)
{
    int code = value->length ? (unsigned char)*bytes(value) : -1;
    release(value);
    return code;
}

int ib_string_chr(struct ib_string *value, int code)
{
    if (code < 0 || code > 255) return IB_RANGE;
    scratch[0] = (char)code;
    return result_string(value, 1);
}

int ib_string_number(struct ib_string *value, number_t number)
{
    unsigned length = writenumber2(scratch, number);
    return result_string(value, length);
}

int ib_string_value(const struct ib_string *value, number_t *number)
{
    char *text = scratch;
    unsigned negative = 0;
    unsigned length = value->length, used;
    copy(scratch, bytes(value), length);
    scratch[length] = 0;
    release(value);
    while (*text == ' ' || *text == '\t') ++text;
    if (*text == '-' || *text == '+') negative = *text++ == '-';
    used = parsenumber2(text, number);
    if (!used) return IB_SYNTAX;
    text += used;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text) return IB_SYNTAX;
    if (negative) *number = -*number;
    return IB_OK;
}

int ib_string_slice(struct ib_string *value, unsigned op, int start, int count)
{
    unsigned offset, length;
    if (count < 0 || start < 1) return IB_RANGE;
    length = (unsigned)count < value->length ? (unsigned)count : value->length;
    offset = op == IB_SRIGHT ? value->length - length : (unsigned)(start - 1);
    if (offset >= value->length) {
        offset = value->length;
        length = 0;
    } else if (length > value->length - offset) {
        length = value->length - offset;
    }
    copy(scratch, bytes(value) + offset, length);
    release(value);
    return result_string(value, length);
}
