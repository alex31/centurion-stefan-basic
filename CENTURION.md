# Stefan BASIC for Centurion

This branch ports Stefan Lenz's BASIC to CENTOS. The `upstream` branch is a
source import of the six interpreter files used by the port, from the POSIX
target at commit `86ec458ca3dac9d156ce23bfa28a99f1f39ca656`.
[Upstream revision](https://github.com/slviajero/tinybasic/tree/86ec458ca3dac9d156ce23bfa28a99f1f39ca656/Basic2/Posix)

- The interpreter stores tokenized lines in a 4 KiB RAM arena and reads and
  writes native CENTOS text files. The console supports line editing, `.DIR`,
  `.LOAD`, `.SAVE` and `.RENUM`.
- The compiler and bytecode VM execute stored programs and direct commands.
  Unqualified numeric variables start as float32. A `DEFINT` header selects
  16-bit integers for the named variables, and `DIM` supports numeric arrays.
  With `DEFINT`, whole-valued literals in the signed 16-bit range also use
  the integer type. The operators `+`, `-`, `*` and `/` use integer arithmetic
  when both operands are integers and promote mixed operands to float32.
  Integer division truncates toward zero. Use a real variable as an operand
  to obtain a fractional quotient when both values are whole numbers.
  The VM includes real arithmetic, comparisons, power and the scalar math
  functions used by the console build.
- `A$` through `Z$` are independent string variables. They support assignment,
  concatenation, comparison, input and output, and the functions `LEN`,
  `ASC`, `CHR$`, `LEFT$`, `RIGHT$`, `MID$`, `STR$` and `VAL`. Each string
  can contain up to 255 characters. The variables share 4 KiB of string
  storage. The VM uses an additional bounded buffer for temporary results.
- `DATA` defines signed numeric literals and quoted strings in stored
  programs. `READ` assigns them in source order to numeric scalars and
  arrays, or to string variables. `RESTORE` rewinds to the beginning.
  `RESTORE n` selects the n-th value, starting at 1. The data cursor reads
  the stored tokens directly and reports type errors and end of data.
- `WHILE` tests a condition before each iteration and `WEND` returns to
  that test. Conditions can compare real numbers and strings. `WHILE`
  and `FOR` share a nesting limit of eight loops in the compiler. A
  `WHILE` loop compiles to branches and needs no runtime loop frame.
- The REPL and line editor share a code overlay. The compiler and VM run
  from their own overlays while variables live in permanent RAM.
- The VM overlay includes real arithmetic and conversions. Real FOR
  loops with exact whole-number values and a unit increment can advance
  their binary32 representation directly. Other steps and fractional or
  out-of-range values use the full floating-point operations.
- The distribution keeps trigonometric reduction and logarithms mapped
  during bytecode execution and places sine, cosine, tangent, square
  roots and random number generation with the VM. Finite real squares
  use the native binary32 multiplication path.
  The compact build keeps the 4 KiB program arena and the 20 KiB CENTOS
  module budget.
- The CPU6 assembly dispatch loop keeps the instruction and value-stack
  pointers in registers. It copies real literals and scalars directly and
  handles common indexed operations and integer arithmetic. Real-to-integer
  conversion uses the encoded exponent and fraction with signed bounds.
- The native dispatcher appends a single literal character in place when
  a nonempty string ends at the heap tail. The C string runtime handles
  compaction, longer literals and the existing length and capacity errors.
- The CENTOS console batches up to 80 characters in one write. Bytecode
  `PRINT` passes strings to the terminal buffer by length. The terminal
  tests check spaces across a full buffer and newlines inside a string.
- On the microcoded CPU6, the 26-generation integer Life kernel went from
  59,110,987 to 18,707,661 microsteps in the development measurements, a
  68.35% reduction. Both runs used the same seed, rule and generation count.
  This is a kernel measurement on an emulator.

Run `make basic` from the distribution root to produce the executable,
mapped module and overlays with the SDK's native compiler runtime. Run
`make basic-image SEED=/path/to/CENTOS_13.IMG`
to install and boot-test BASIC on a new disk copy. Use `STARTBASIC` at the
CENTOS prompt.

The original [LICENSE](LICENSE) and source notices are retained. The original
root license is BSD-3-Clause. Some interpreter headers also mention GPLv3.
See [the provenance manifest](PROVENANCE.json).
