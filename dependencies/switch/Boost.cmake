# Switch only needs the following boost libraries, not all of them present upstream
set(_boost_libs
	algorithm align array assert atomic bind circular_buffer compat concept_check config container
	container_hash conversion core describe detail dynamic_bitset exception filesystem function
	function_types functional fusion integer intrusive io iterator move mp11 mpl nowide optional
	parameter predef preprocessor ptr_container random range regex scope signals2 smart_ptr
	static_assert static_string system throw_exception tokenizer tuple type_index type_traits typeof
	unordered utility variant variant2 winapi)
set(_boost_root "${CMAKE_CURRENT_SOURCE_DIR}/boost")

if(NOT EXISTS "${_boost_root}/.gitmodules")
	message(STATUS "Checking out the Boost superproject")
	execute_process(
		COMMAND git submodule update --init --checkout --depth 1 -- dependencies/switch/boost
		WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
		COMMAND_ERROR_IS_FATAL ANY)
endif()

set(_boost_missing "")
foreach(_lib IN LISTS _boost_libs)
	if(NOT EXISTS "${_boost_root}/libs/${_lib}/include")
		list(APPEND _boost_missing "libs/${_lib}")
	endif()
endforeach()
if(_boost_missing)
	list(LENGTH _boost_missing _boost_missing_count)
	message(STATUS "Checking out ${_boost_missing_count} Boost libraries")
	execute_process(
		COMMAND git submodule update --init --depth 1 -- ${_boost_missing}
		WORKING_DIRECTORY "${_boost_root}"
		COMMAND_ERROR_IS_FATAL ANY)
endif()

add_library(switch_boost_headers INTERFACE)
add_library(Boost::headers ALIAS switch_boost_headers)
foreach(_lib IN LISTS _boost_libs)
	target_include_directories(switch_boost_headers SYSTEM INTERFACE "${_boost_root}/libs/${_lib}/include")
endforeach()
target_compile_definitions(switch_boost_headers INTERFACE BOOST_ALL_NO_LIB)

file(GLOB _nowide_sources CONFIGURE_DEPENDS "${_boost_root}/libs/nowide/src/*.cpp")
add_library(switch_boost_nowide STATIC ${_nowide_sources})
add_library(Boost::nowide ALIAS switch_boost_nowide)
target_link_libraries(switch_boost_nowide PUBLIC switch_boost_headers)
