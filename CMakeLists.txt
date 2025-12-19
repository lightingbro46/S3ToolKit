project(S3ToolKit)
cmake_minimum_required(VERSION 3.1.3)

# enable c++11
set(CMAKE_CXX_STANDARD 11)
# -fPIC
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Print details
set(CMAKE_VERBOSE_MAKEFILE ON)

#Load custom module
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(ENABLE_OPENSSL "enable openssl" ON)
option(ENABLE_MYSQL "enable mysql" ON)
option(ENABLE_SQLITE "enable sqlite" ON)
option(ENABLE_WEPOLL "Enable wepoll" ON)
option(ASAN_USE_DELETE "use delele[] or free when asan enabled" OFF)
option(BUILD_SHARED_LIBS "Build all libraries shared" ON)

include(CheckStructHasMember)
include(CheckSymbolExists)

list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_struct_has_member("struct mmsghdr" msg_hdr sys/socket.h HAVE_MMSG_HDR)
check_symbol_exists(sendmmsg sys/socket.h HAVE_SENDMMSG_API)
check_symbol_exists(recvmmsg sys/socket.h HAVE_RECVMMSG_API)

# Conveniently modify global variables
function(update_cached name value)
    set("${name}" "${value}" CACHE INTERNAL "*** Internal ***" FORCE)
endfunction()

function(update_cached_list name)
    set(_tmp_list "${${name}}")
    list(APPEND _tmp_list "${ARGN}")
    list(REMOVE_DUPLICATES _tmp_list)
    update_cached(${name} "${_tmp_list}")
endfunction()

update_cached(TK_INC_PATHS "")
update_cached(TK_LINK_LIBRARIES "")
update_cached(TK_COMPILE_DEFINITIONS "")
update_cached(TK_COMPILE_OPTIONS "")

if (HAVE_MMSG_HDR)
    update_cached_list(TK_COMPILE_DEFINITIONS HAVE_MMSG_HDR)
endif ()

if (HAVE_SENDMMSG_API)
    update_cached_list(TK_COMPILE_DEFINITIONS HAVE_SENDMMSG_API)
endif ()

if (HAVE_RECVMMSG_API)
    update_cached_list(TK_COMPILE_DEFINITIONS HAVE_RECVMMSG_API)
endif ()

# check the socket buffer size set by the upper cmake project, if it is set, use the setting of the upper cmake project, otherwise set it to 256K
# if the socket buffer size is set to 0, it means that the socket buffer size is not set, and the kernel default value is used(just for linux)
if (DEFINED SOCKET_DEFAULT_BUF_SIZE)
    if (SOCKET_DEFAULT_BUF_SIZE EQUAL 0)
        message(STATUS "Socket default buffer size is not set, use the kernel default value")
    else ()
        message(STATUS "Socket default buffer size is set to ${SOCKET_DEFAULT_BUF_SIZE}")
    endif ()
    update_cached_list(TK_COMPILE_DEFINITIONS SOCKET_DEFAULT_BUF_SIZE=${SOCKET_DEFAULT_BUF_SIZE})
endif ()

# Collect source code
file(GLOB SRC_LIST
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*/*/*.cpp)

if (WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    if (MSVC)
        update_cached_list(TK_COMPILE_OPTIONS "/utf-8")
    endif ()
    update_cached_list(TK_LINK_LIBRARIES WS2_32 Iphlpapi shlwapi)
    # Prevent Windows.h from including Winsock.h
    update_cached_list(TK_COMPILE_DEFINITIONS WIN32_LEAN_AND_MEAN MP4V2_NO_STDINT_DEFS _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
else ()
    # Non-Windows platforms come with getopt api
    list(FILTER SRC_LIST EXCLUDE REGEX "getopt.c$")
    update_cached_list(TK_COMPILE_OPTIONS "-Wno-comment" "-Wno-deprecated-declarations" "-Wno-predefined-identifier-outside-function")
endif ()

if (NOT WIN32 OR NOT ENABLE_WEPOLL)
    # Remove wepoll
    list(FILTER SRC_LIST EXCLUDE REGEX "wepoll.c$")
else ()
    update_cached_list(TK_COMPILE_DEFINITIONS HAS_EPOLL)
    update_cached_list(TK_INC_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src/win32/)
endif ()

# Remove .mm type files from non-Apple platforms
if (NOT APPLE)
    list(FILTER SRC_LIST EXCLUDE REGEX "Socket_ios.mm$")
endif ()

# Find whether openssl is installed
if (ENABLE_OPENSSL)
    find_package(OpenSSL)
    if (OPENSSL_FOUND)
        update_cached_list(TK_INC_PATHS ${OPENSSL_INCLUDE_DIR})
        update_cached_list(TK_LINK_LIBRARIES ${OPENSSL_LIBRARIES})
        update_cached_list(TK_COMPILE_DEFINITIONS ENABLE_OPENSSL)
    endif ()
endif ()

# Find whether mysql is installed
if (ENABLE_MYSQL)
    find_package(MYSQL)
    if (MYSQL_FOUND)
        update_cached_list(TK_INC_PATHS ${MYSQL_INCLUDE_DIR})
        update_cached_list(TK_INC_PATHS ${MYSQL_INCLUDE_DIR}/mysql)
        update_cached_list(TK_LINK_LIBRARIES ${MYSQL_LIBRARIES})
        update_cached_list(TK_COMPILE_DEFINITIONS ENABLE_MYSQL)
    endif ()
endif ()

#Find out if SQLite3 is installed
if (ENABLE_SQLITE)
    find_package(SQLite3 QUIET)
    if (SQLite3_FOUND)
        update_cached_list(TK_INC_PATHS ${SQLite3_INCLUDE_DIR})
        update_cached_list(TK_LINK_LIBRARIES ${SQLite3_LIBRARIES})
        update_cached_list(TK_COMPILE_DEFINITIONS ENABLE_SQLITE)
    endif ()
endif ()

# Whether to use delete[] instead of free to solve the problem of freezing on MacOS after enabling asan
if (ASAN_USE_DELETE)
    update_cached_list(TK_COMPILE_DEFINITIONS ASAN_USE_DELETE)
endif ()

# Library dependencies
add_library(${PROJECT_NAME}_deps INTERFACE)
target_link_libraries(${PROJECT_NAME}_deps INTERFACE ${TK_LINK_LIBRARIES})

# Compile library
add_library(${PROJECT_NAME} ${SRC_LIST})
# Include header file paths
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${TK_INC_PATHS})
target_link_libraries(${PROJECT_NAME} PUBLIC ${PROJECT_NAME}_deps)
target_compile_definitions(${PROJECT_NAME} PUBLIC ${TK_COMPILE_DEFINITIONS})
target_compile_options(${PROJECT_NAME} PUBLIC ${TK_COMPILE_OPTIONS})
set_target_properties(${PROJECT_NAME} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib
                                                 ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # This project is the root project, so add the test program
    add_subdirectory(tests)
endif ()