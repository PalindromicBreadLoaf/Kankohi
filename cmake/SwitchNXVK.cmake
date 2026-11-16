execute_process(
	COMMAND "${PKG_CONFIG_EXECUTABLE}" --exists nxvk
	RESULT_VARIABLE _nxvk_missing)
if(_nxvk_missing)
	message(FATAL_ERROR "The nxvk portlib wasn't found by ${PKG_CONFIG_EXECUTABLE}.")
endif()

execute_process(
	COMMAND "${PKG_CONFIG_EXECUTABLE}" --cflags nxvk
	OUTPUT_VARIABLE _nxvk_cflags
	OUTPUT_STRIP_TRAILING_WHITESPACE
	COMMAND_ERROR_IS_FATAL ANY)
execute_process(
	COMMAND "${PKG_CONFIG_EXECUTABLE}" --libs --static nxvk
	OUTPUT_VARIABLE _nxvk_libs
	OUTPUT_STRIP_TRAILING_WHITESPACE
	COMMAND_ERROR_IS_FATAL ANY)
separate_arguments(_nxvk_cflags UNIX_COMMAND "${_nxvk_cflags}")
separate_arguments(_nxvk_libs UNIX_COMMAND "${_nxvk_libs}")

set(_nxvk_includes "")
set(_nxvk_options "")
foreach(_flag IN LISTS _nxvk_cflags)
	if(_flag MATCHES "^-I(.+)$")
		list(APPEND _nxvk_includes "${CMAKE_MATCH_1}")
	else()
		list(APPEND _nxvk_options "${_flag}")
	endif()
endforeach()

add_library(nxvk INTERFACE)
add_library(nxvk::nxvk ALIAS nxvk)
target_include_directories(nxvk SYSTEM INTERFACE ${_nxvk_includes})
target_compile_options(nxvk INTERFACE ${_nxvk_options})
target_link_libraries(nxvk INTERFACE ${_nxvk_libs})
