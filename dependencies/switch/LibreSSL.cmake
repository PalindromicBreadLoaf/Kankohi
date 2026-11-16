include(FetchContent)
set(LIBRESSL_VERSION 4.1.0)
FetchContent_Declare(libressl
	URL "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-${LIBRESSL_VERSION}.tar.gz"
	    "https://github.com/libressl/portable/releases/download/v${LIBRESSL_VERSION}/libressl-${LIBRESSL_VERSION}.tar.gz"
	URL_HASH SHA256=0f71c16bd34bdaaccdcb96a5d94a4921bfb612ec6e0eba7a80d8854eefd8bb61
	DOWNLOAD_EXTRACT_TIMESTAMP ON
	EXCLUDE_FROM_ALL
	SYSTEM)

set(LIBRESSL_APPS OFF CACHE BOOL "" FORCE)
set(LIBRESSL_TESTS OFF CACHE BOOL "" FORCE)
set(LIBRESSL_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(HAVE_ARC4RANDOM_BUF 1 CACHE INTERNAL "")
set(HAVE_ARC4RANDOM_UNIFORM 1 CACHE INTERNAL "")
set(HAVE_GETENTROPY 1 CACHE INTERNAL "")
set(HAVE_ENDIAN_H 1 CACHE INTERNAL "")
FetchContent_MakeAvailable(libressl)

target_sources(crypto_obj PRIVATE
	"${libressl_SOURCE_DIR}/crypto/crypto_lock.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/libressl_horizon/ui_horizon.c")
foreach(_target crypto_obj ssl_obj compat_obj)
	if(TARGET ${_target})
		target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libressl_horizon")
	endif()
endforeach()

add_library(OpenSSL::Crypto ALIAS crypto)
add_library(OpenSSL::SSL ALIAS ssl)
