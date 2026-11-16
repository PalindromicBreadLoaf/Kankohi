// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define FIBER_FRAME_X19 0x00
#define FIBER_FRAME_X29 0x50
#define FIBER_FRAME_D8 0x60
#define FIBER_FRAME_FPCR 0xA0
#define FIBER_FRAME_SIZE 0xB0

#ifndef __ASSEMBLER__
extern "C"
{
	// Saves the callee-saved state on the current stack, stores the stack pointer to *saveSp and
	// resumes the context at loadSp.
	void HorizonFiber_Switch(void** saveSp, void* loadSp);
	// See above but without FPCR
	void HorizonFiber_SwitchNoFpcr(void** saveSp, void* loadSp);
	// Builds the first context of a fibre at stackTop (16-byte aligned).
	void* HorizonFiber_InitStack(void* stackTop, void (*entry)(void*), void* userParam);
}
#endif
