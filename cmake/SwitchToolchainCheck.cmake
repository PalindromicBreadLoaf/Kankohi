# SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" OR NOT NINTENDO_SWITCH)
	message(FATAL_ERROR "Not configured for the Switch (system ${CMAKE_SYSTEM_NAME}). Pass -DCMAKE_TOOLCHAIN_FILE=$ENV{DEVKITPRO}/cmake/Switch.cmake.")
endif()

foreach(_lang C CXX)
	if(CMAKE_${_lang}_COMPILER AND NOT CMAKE_${_lang}_COMPILER MATCHES "aarch64-none-elf-")
		message(FATAL_ERROR "The ${_lang} compiler is ${CMAKE_${_lang}_COMPILER}, not devkitA64. Pass CMAKE_${_lang}_COMPILER explicitly.")
	endif()
endforeach()

message(STATUS "Switch toolchain: ${CMAKE_CXX_COMPILER} (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}), with libnx in ${NX_ROOT}")
