// SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Fiber.h"
#include "FiberContextSwitch.h"

thread_local Fiber* sCurrentFiber{};

static constexpr size_t FIBER_STACK_SIZE = 2 * 1024 * 1024;

Fiber::Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData) : m_privateData(privateData)
{
	m_stackPtr = std::aligned_alloc(16, FIBER_STACK_SIZE);
	cemu_assert(m_stackPtr != nullptr);
	m_implData = HorizonFiber_InitStack((uint8*)m_stackPtr + FIBER_STACK_SIZE, FiberEntryPoint, userParam);
}

Fiber::Fiber(void* privateData) : m_privateData(privateData)
{
	// m_implData receives the thread's stack pointer when it first switches away
}

Fiber::~Fiber()
{
	if (m_stackPtr)
		std::free(m_stackPtr);
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
	cemu_assert_debug(sCurrentFiber == nullptr);
	sCurrentFiber = new Fiber(privateData);
	return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
	Fiber* leavingFiber = sCurrentFiber;
	cemu_assert_debug(leavingFiber != &targetFiber);
	sCurrentFiber = &targetFiber;
	std::atomic_thread_fence(std::memory_order_seq_cst);
	HorizonFiber_Switch(&leavingFiber->m_implData, targetFiber.m_implData);
	std::atomic_thread_fence(std::memory_order_seq_cst);
}

void* Fiber::GetFiberPrivateData()
{
	return sCurrentFiber->m_privateData;
}
