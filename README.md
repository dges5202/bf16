# Quiz 1 B: BFloat16

Host C first, then the same routines on rv32emu. No `float` arithmetic inside the bodies: binary32 is a `uint32_t` bit pattern.

```bash
make test
```

Fill the stubs in [`bf16.c`](bf16.c). Types, masks, and the tests are already there. On the host, integer `*` is allowed for the 8-bit mantissa product; the RV32I guest will not have `mul`.

Expected: every line `PASS`, then `0 failed`.

Solution write-up: [Quiz 1](https://hackmd.io/@sysprog/arch2026-quiz1).
