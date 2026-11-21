# SPDX-FileCopyrightText: 2026 PalindromicBreadLoaf
# SPDX-License-Identifier: GPL-3.0-or-later

find_program(UAM_EXECUTABLE uam HINTS "${DEVKITPRO}/tools/bin" REQUIRED)

set(_brls "${CMAKE_CURRENT_SOURCE_DIR}/borealis/library")
set(_brls_extern "${_brls}/lib/extern")

file(GLOB_RECURSE _yoga_sources CONFIGURE_DEPENDS "${_brls_extern}/yoga/yoga/*.cpp")
add_library(brls_yoga STATIC ${_yoga_sources})
target_include_directories(brls_yoga SYSTEM PUBLIC "${_brls_extern}/yoga")
target_compile_definitions(brls_yoga PUBLIC YG_ENABLE_EVENTS)
target_compile_options(brls_yoga PRIVATE -w)
set_target_properties(brls_yoga PROPERTIES CXX_STANDARD 20)

add_library(brls_tinyxml2 STATIC "${_brls_extern}/tinyxml2/tinyxml2.cpp")
target_include_directories(brls_tinyxml2 SYSTEM PUBLIC "${_brls}/include/borealis/extern/tinyxml2")
# Cemu links its own, older tinyxml2
target_compile_definitions(brls_tinyxml2 PUBLIC tinyxml2=brls_tinyxml2)
target_compile_options(brls_tinyxml2 PRIVATE -w)

file(GLOB _brls_sources CONFIGURE_DEPENDS
	"${_brls}/lib/core/*.cpp"
	"${_brls}/lib/core/touch/*.cpp"
	"${_brls}/lib/views/*.cpp"
	"${_brls}/lib/views/cells/*.cpp"
	"${_brls}/lib/views/widgets/*.cpp"
	"${_brls}/lib/platforms/switch/*.cpp"
	"${_brls_extern}/libretro-common/compat/*.c"
	"${_brls_extern}/libretro-common/encodings/*.c"
	"${_brls_extern}/libretro-common/features/*.c"
	"${_brls_extern}/switch-libpulsar/src/*/*.c"
	"${_brls_extern}/nanovg/deko3d/*.cpp"
	"${_brls_extern}/nanovg/deko3d/framework/*.cpp")
list(APPEND _brls_sources "${_brls_extern}/nanovg/nanovg.c")
set_source_files_properties("${_brls_extern}/nanovg/nanovg.c" PROPERTIES COMPILE_DEFINITIONS STB_IMAGE_STATIC)

add_library(borealis STATIC ${_brls_sources})
target_include_directories(borealis SYSTEM PUBLIC
	"${_brls}/include"
	"${_brls}/include/borealis/extern"
	"${_brls}/include/borealis/extern/nanovg"
	"${_brls_extern}/tweeny/include"
	"${_brls_extern}/switch-libpulsar/include")
target_compile_definitions(borealis PUBLIC
	BRLS_RESOURCES="romfs:/"
	BOREALIS_USE_DEKO3D
	HAVE_LIBNX
	SWITCH
	STBI_NO_THREAD_LOCALS)
target_compile_options(borealis PRIVATE -w)
target_link_libraries(borealis PUBLIC brls_yoga brls_tinyxml2 fmt::fmt glm::glm deko3d)
set_target_properties(borealis PROPERTIES CXX_STANDARD 17)

set(BOREALIS_ROMFS_DIR "${CMAKE_BINARY_DIR}/borealis_romfs")
set(BOREALIS_ROMFS_FILES)

set(_shader_dir "${_brls_extern}/nanovg/deko3d/shaders")
foreach(_shader fill_vsh:vert fill_fsh:frag fill_aa_fsh:frag)
	string(REPLACE ":" ";" _shader "${_shader}")
	list(GET _shader 0 _name)
	list(GET _shader 1 _stage)
	set(_out "${BOREALIS_ROMFS_DIR}/shaders/${_name}.dksh")
	add_custom_command(OUTPUT "${_out}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${BOREALIS_ROMFS_DIR}/shaders"
		COMMAND "${UAM_EXECUTABLE}" -s ${_stage} -o "${_out}" "${_shader_dir}/${_name}.glsl"
		DEPENDS "${_shader_dir}/${_name}.glsl"
		COMMENT "Compiling NanoVG shader ${_name}"
		VERBATIM)
	list(APPEND BOREALIS_ROMFS_FILES "${_out}")
endforeach()

foreach(_asset material/MaterialIcons-Regular.ttf img/sys/cursor.png i18n/en-US/hints.json)
	set(_out "${BOREALIS_ROMFS_DIR}/${_asset}")
	add_custom_command(OUTPUT "${_out}"
		COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/borealis/resources/${_asset}" "${_out}"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/borealis/resources/${_asset}"
		VERBATIM)
	list(APPEND BOREALIS_ROMFS_FILES "${_out}")
endforeach()

add_custom_target(borealis_romfs_files DEPENDS ${BOREALIS_ROMFS_FILES})
