# Stefan BASIC for Centurion

This branch ports Stefan Lenz's BASIC to CENTOS. The `upstream` branch is a
source import of the six interpreter files used by the port, from the POSIX
target at commit `86ec458ca3dac9d156ce23bfa28a99f1f39ca656`.
[Upstream revision](https://github.com/slviajero/tinybasic/tree/86ec458ca3dac9d156ce23bfa28a99f1f39ca656/Basic2/Posix)

- The interpreter stores tokenized lines in a 4 KiB RAM arena and reads and
  writes native CENTOS text files. The console supports line editing, `.DIR`,
  `.LOAD`, `.SAVE` and `.RENUM`.
- The CENTOS application uses SDCC's float32 runtime, a mapped module and
  partial overlays. Untyped programs and prompt commands use the interpreter.
  Programs with `DEFINT` or `DIM` headers can run through a typed bytecode VM.
- The CPU6 assembly dispatch loop keeps the instruction and value-stack
  pointers in registers. It combines common indexed operations and handles
  the arithmetic hot path before falling back to C for float32 and I/O.
- On the microcoded CPU6, the 26-generation integer Life kernel went from
  59,110,987 to 18,707,661 microsteps in the development measurements, a
  68.35% reduction. Both runs used the same seed, rule and generation count.
  This is a kernel measurement on an emulator.

Run `make basic` from the distribution root to produce the executable,
mapped module and overlays. Run `make basic-image SEED=/path/to/CENTOS_13.IMG`
to install and boot-test BASIC on a new disk copy. Use `STARTBASIC` at the
CENTOS prompt.

The original [LICENSE](LICENSE) and source notices are retained. The original
root license is BSD-3-Clause. Some interpreter headers also mention GPLv3.
See [the provenance manifest](PROVENANCE.json).
