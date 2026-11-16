list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)

set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
add_subdirectory(fmt EXCLUDE_FROM_ALL SYSTEM)

set(PUGIXML_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(pugixml EXCLUDE_FROM_ALL SYSTEM)

add_library(switch_glm INTERFACE)
add_library(glm::glm ALIAS switch_glm)
target_include_directories(switch_glm SYSTEM INTERFACE glm)

file(GLOB _zstd_sources CONFIGURE_DEPENDS
	zstd/lib/common/*.c
	zstd/lib/compress/*.c
	zstd/lib/decompress/*.c)
add_library(switch_zstd STATIC ${_zstd_sources})
add_library(zstd::zstd ALIAS switch_zstd)
target_include_directories(switch_zstd SYSTEM PUBLIC zstd/lib)
target_compile_definitions(switch_zstd PRIVATE ZSTD_DISABLE_ASM ZSTD_MULTITHREAD=0)

include(Boost.cmake)

set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(UNIX TRUE)
add_subdirectory(glslang EXCLUDE_FROM_ALL SYSTEM)
unset(UNIX)
target_include_directories(glslang SYSTEM INTERFACE glslang_include)

include(LibreSSL.cmake)

add_library(xbyak_aarch64 STATIC ${PROJECT_SOURCE_DIR}/dependencies/xbyak_aarch64/src/xbyak_aarch64_impl.cpp)
target_include_directories(xbyak_aarch64 SYSTEM PUBLIC
	${PROJECT_SOURCE_DIR}/dependencies/xbyak_aarch64
	${PROJECT_SOURCE_DIR}/dependencies/xbyak_aarch64/src
	${PROJECT_SOURCE_DIR}/dependencies/xbyak_aarch64/xbyak_aarch64)
target_include_directories(xbyak_aarch64 SYSTEM AFTER PUBLIC xbyak_aarch64_horizon)
