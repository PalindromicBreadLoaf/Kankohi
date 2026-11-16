# Platform block for Nintendo Switch builds.
include(${CMAKE_CURRENT_LIST_DIR}/SwitchToolchainCheck.cmake)

if(ENABLE_VCPKG)
	message(FATAL_ERROR "vcpkg can't target the Switch. (Re)configure with -DENABLE_VCPKG=OFF.")
endif()

enable_language(ASM)

macro(_switch_force var value)
	set(${var} ${value} CACHE BOOL "Forced by the Switch platform block" FORCE)
endmacro()

_switch_force(ENABLE_WXWIDGETS OFF)
_switch_force(ENABLE_OPENGL OFF)
_switch_force(ENABLE_METAL OFF)
_switch_force(ENABLE_DISCORD_RPC OFF)
_switch_force(ENABLE_HIDAPI OFF)
_switch_force(ENABLE_LIBUSB OFF)
_switch_force(ENABLE_CUBEB OFF)
_switch_force(ENABLE_SDL OFF)
_switch_force(ENABLE_WAYLAND OFF)
_switch_force(ENABLE_BLUEZ OFF)
_switch_force(ENABLE_FERAL_GAMEMODE OFF)
_switch_force(ENABLE_VULKAN ON)

option(ENABLE_NXVK "Enable the Vulkan backend via NXVK" ON)
option(ENABLE_DEKO3D "Enable the deko3d renderer" ON)
if(ENABLE_VULKAN AND NOT ENABLE_NXVK)
	message(FATAL_ERROR "NXVK is required for Vulkan support.")
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" _switch_build_type)
if(_switch_build_type STREQUAL "debug")
	_switch_force(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
else()
	_switch_force(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

add_compile_definitions(_GNU_SOURCE)

set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

if(ENABLE_NXVK)
	include(${CMAKE_CURRENT_LIST_DIR}/SwitchNXVK.cmake)
endif()
if(ENABLE_DEKO3D)
	include(${CMAKE_CURRENT_LIST_DIR}/SwitchDeko3D.cmake)
	add_compile_definitions(ENABLE_DEKO3D)
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/dependencies/switch ${PROJECT_BINARY_DIR}/dependencies/switch)
