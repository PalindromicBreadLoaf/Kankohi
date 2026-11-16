# SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
# SPDX-License-Identifier: GPL-3.0-or-later

set(_deko3d_files
	"${NX_ROOT}/include/deko3d.h"
	"${NX_ROOT}/include/deko3d.hpp"
	"${NX_ROOT}/lib/libdeko3d.a"
	"${NX_ROOT}/lib/libdeko3dd.a")
foreach(_file IN LISTS _deko3d_files)
	if(NOT EXISTS "${_file}")
		message(FATAL_ERROR "deko3d is missing ${_file}. ([dkp-]pacman -S deko3d)")
	endif()
endforeach()

add_library(deko3d INTERFACE)
add_library(deko3d::deko3d ALIAS deko3d)
target_include_directories(deko3d SYSTEM INTERFACE "${NX_ROOT}/include")
target_link_libraries(deko3d INTERFACE
	"$<IF:$<CONFIG:Debug>,${NX_ROOT}/lib/libdeko3dd.a,${NX_ROOT}/lib/libdeko3d.a>")
target_compile_definitions(deko3d INTERFACE DK_HPP_SUPPORT_VECTOR)
