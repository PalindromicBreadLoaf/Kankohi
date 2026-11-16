# SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
# SPDX-License-Identifier: GPL-3.0-or-later

# Copies the ELF from the previous build into ARCHIVE_DIR. For debugging.
if(NOT EXISTS "${ELF}")
	return()
endif()
execute_process(COMMAND "${READELF}" -n "${ELF}" OUTPUT_VARIABLE _notes ERROR_QUIET)
string(REGEX MATCH "Build ID: ([0-9a-f]+)" _match "${_notes}")
if(NOT CMAKE_MATCH_1)
	message(WARNING "No build id in ${ELF}, not archiving it")
	return()
endif()
get_filename_component(_name "${ELF}" NAME_WE)
set(_archived "${ARCHIVE_DIR}/${_name}-${CMAKE_MATCH_1}.elf")
if(NOT EXISTS "${_archived}")
	file(MAKE_DIRECTORY "${ARCHIVE_DIR}")
	file(COPY_FILE "${ELF}" "${_archived}")
endif()
