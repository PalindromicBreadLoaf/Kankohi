#pragma once
#include "Common/precompiled.h"

#ifdef _WIN32
#include "Common/windows/FileStream_win32.h"
#elif defined(__SWITCH__)
#include "Common/switch/FileStream_switch.h"
#else
#include "Common/unix/FileStream_unix.h"
#endif
