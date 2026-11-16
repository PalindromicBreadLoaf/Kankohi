#include "FiberSwitch.h"

.macro SAVE_CONTEXT
  sub sp, sp, #FIBER_FRAME_SIZE
  stp x19, x20, [sp, #(FIBER_FRAME_X19 + 0x00)]
  stp x21, x22, [sp, #(FIBER_FRAME_X19 + 0x10)]
  stp x23, x24, [sp, #(FIBER_FRAME_X19 + 0x20)]
  stp x25, x26, [sp, #(FIBER_FRAME_X19 + 0x30)]
  stp x27, x28, [sp, #(FIBER_FRAME_X19 + 0x40)]
  stp x29, x30, [sp, #FIBER_FRAME_X29]
  stp d8, d9, [sp, #(FIBER_FRAME_D8 + 0x00)]
  stp d10, d11, [sp, #(FIBER_FRAME_D8 + 0x10)]
  stp d12, d13, [sp, #(FIBER_FRAME_D8 + 0x20)]
  stp d14, d15, [sp, #(FIBER_FRAME_D8 + 0x30)]
.endm

.macro RESTORE_CONTEXT
  ldp x19, x20, [sp, #(FIBER_FRAME_X19 + 0x00)]
  ldp x21, x22, [sp, #(FIBER_FRAME_X19 + 0x10)]
  ldp x23, x24, [sp, #(FIBER_FRAME_X19 + 0x20)]
  ldp x25, x26, [sp, #(FIBER_FRAME_X19 + 0x30)]
  ldp x27, x28, [sp, #(FIBER_FRAME_X19 + 0x40)]
  ldp x29, x30, [sp, #FIBER_FRAME_X29]
  ldp d8, d9, [sp, #(FIBER_FRAME_D8 + 0x00)]
  ldp d10, d11, [sp, #(FIBER_FRAME_D8 + 0x10)]
  ldp d12, d13, [sp, #(FIBER_FRAME_D8 + 0x20)]
  ldp d14, d15, [sp, #(FIBER_FRAME_D8 + 0x30)]
  add sp, sp, #FIBER_FRAME_SIZE
.endm

.text

.balign 16
.global HorizonFiber_Switch
.type HorizonFiber_Switch, %function
HorizonFiber_Switch:
  SAVE_CONTEXT
  mrs x2, fpcr
  str x2, [sp, #FIBER_FRAME_FPCR]
  mov x3, sp
  str x3, [x0]
  mov sp, x1
  ldr x3, [sp, #FIBER_FRAME_FPCR]
  // Skip FPCR when the mode is unchanged
  cmp x2, x3
  b.eq 1f
  msr fpcr, x3
1:
  RESTORE_CONTEXT
  ret
.size HorizonFiber_Switch, . - HorizonFiber_Switch

.balign 16
.global HorizonFiber_SwitchNoFpcr
.type HorizonFiber_SwitchNoFpcr, %function
HorizonFiber_SwitchNoFpcr:
  SAVE_CONTEXT
  mov x3, sp
  str x3, [x0]
  mov sp, x1
  RESTORE_CONTEXT
  ret
.size HorizonFiber_SwitchNoFpcr, . - HorizonFiber_SwitchNoFpcr

.balign 16
.global HorizonFiber_InitStack
.type HorizonFiber_InitStack, %function
HorizonFiber_InitStack:
  and x0, x0, #~0xF
  sub x0, x0, #FIBER_FRAME_SIZE
  mov x3, x0
  mov x4, #(FIBER_FRAME_SIZE / 16)
1:
  stp xzr, xzr, [x3], #16
  subs x4, x4, #1
  b.ne 1b
  stp x1, x2, [x0, #FIBER_FRAME_X19]
  adrp x3, HorizonFiber_Start
  add x3, x3, :lo12:HorizonFiber_Start
  stp xzr, x3, [x0, #FIBER_FRAME_X29]
  mrs x3, fpcr
  str x3, [x0, #FIBER_FRAME_FPCR]
  ret
.size HorizonFiber_InitStack, . - HorizonFiber_InitStack

.balign 16
.type HorizonFiber_Start, %function
HorizonFiber_Start:
  .cfi_startproc
  .cfi_undefined x30
  mov x0, x20
  blr x19
  brk #0
  .cfi_endproc
.size HorizonFiber_Start, . - HorizonFiber_Start
