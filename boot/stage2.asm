; stage2.asm - Placeholder second-stage loader
; The main loader jumps directly to the 32-bit kernel, so this
; sector is kept as a reserved safety stub that simply halts if
; ever executed.

[BITS 16]

cli
hlt

TIMES 2048 - ($ - $$) DB 0
