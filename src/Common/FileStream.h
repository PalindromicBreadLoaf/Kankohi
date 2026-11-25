#pragma once
#include "Common/precompiled.h"

#ifdef _WIN32
#include "Common/windows/FileStream_win32.h"
#elif defined(__SWITCH__)
#include "Common/switch/FileStream_switch.h"
#else
#include "Common/unix/FileStream_unix.h"
#endif

#ifdef __SWITCH__
void RenameReplacing(const fs::path& source, const fs::path& target, std::error_code& ec);
#else
inline void RenameReplacing(const fs::path& source, const fs::path& target, std::error_code& ec)
{
	fs::rename(source, target, ec);
}
#endif
