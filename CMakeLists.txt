project(S3ToolKit)
cmake_minimum_required(VERSION 3.1.3)

include(CheckStructHasMember)
include(CheckSymbolExists)

list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
check_struct_has_member("struct mmsghdr" msg_hdr sys/socket.h HAVE_MMSG_HDR)
check_symbol_exists(sendmmsg sys/socket.h HAVE_SENDMMSG_API)
check_symbol_exists(recvmmsg sys/socket.h HAVE_RECVMMSG_API)

if (HAVE_MMSG_HDR)
    add_definitions(-DHAVE_MMSG_HDR)
endif ()
if (HAVE_SENDMMSG_API)
    add_definitions(-DHAVE_SENDMMSG_API)
endif ()
if (HAVE_RECVMMSG_API)
    add_definitions(-DHAVE_RECVMMSG_API)
endif ()

# check the socket buffer size set by the upper cmake project, if it is set, use the setting of the upper cmake project, otherwise set it to 256K
# if the socket buffer size is set to 0, it means that the socket buffer size is not set, and the kernel default value is used(just for linux)
if (DEFINED SOCKET_DEFAULT_BUF_SIZE)
    if (SOCKET_DEFAULT_BUF_SIZE EQUAL 0)
        message(STATUS "Socket default buffer size is not set, use the kernel default value")
    else ()
        message(STATUS "Socket default buffer size is set to ${SOCKET_DEFAULT_BUF_SIZE}")
    endif ()
    add_definitions(-DSOCKET_DEFAULT_BUF_SIZE=${SOCKET_DEFAULT_BUF_SIZE})
endif ()
#Load custom modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
#Set the library file path
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
#Set the executable program path
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
#Set subdirectories
set(SUB_DIR_LIST "Network" "Poller" "Thread" "Util")

if (WIN32)
    list(APPEND SUB_DIR_LIST "win32")
    #Prevent Windows.h from including Winsock.h
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    if (MSVC)
        add_compile_options("/utf-8")
    endif ()
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
else ()
    add_compile_options("-Wno-comment")
endif ()

#Installation directory
if (WIN32)
    set(INSTALL_PATH_LIB $ENV{HOME}/${PROJECT_NAME}/lib)
    set(INSTALL_PATH_INCLUDE $ENV{HOME}/${PROJECT_NAME}/include)
else ()
    set(INSTALL_PATH_LIB lib)
    set(INSTALL_PATH_INCLUDE include)
endif ()

foreach (SUB_DIR ${SUB_DIR_LIST})
    #Traverse source file
    aux_source_directory(src/${SUB_DIR} SRC_LIST)
    #Install header file to system directory
    install(DIRECTORY src/${SUB_DIR} DESTINATION ${INSTALL_PATH_INCLUDE} FILES_MATCHING PATTERN "*.h")
endforeach ()

#Remove .mm type files from non-apple platforms
if (NOT APPLE)
    list(REMOVE_ITEM SRC_LIST "src/Network/Socket_ios.mm")
endif ()

if (WIN32)
    set(LINK_LIB_LIST WS2_32 Iphlpapi shlwapi)
else ()
    set(LINK_LIB_LIST)
endif ()


set(ENABLE_OPENSSL ON CACHE BOOL "enable openssl")
set(ENABLE_MYSQL ON CACHE BOOL "enable mysql")
set(ENABLE_SQLITE ON CACHE BOOL "enable sqlite")
set(ASAN_USE_DELETE OFF CACHE BOOL "use delele[] or free when asan enabled")

#Find out if openssl is installed
find_package(OpenSSL QUIET)
if (OPENSSL_FOUND AND ENABLE_OPENSSL)
    message(STATUS "Find the openssl library:\"${OPENSSL_INCLUDE_DIR}\",ENABLE_OPENSSL macro is turned on")
    include_directories(${OPENSSL_INCLUDE_DIR})
    add_definitions(-DENABLE_OPENSSL)
    list(APPEND LINK_LIB_LIST ${OPENSSL_LIBRARIES})
endif ()

#Find out if mysql is installed
find_package(MYSQL QUIET)
if (MYSQL_FOUND AND ENABLE_MYSQL)
    message(STATUS "Find the mysqlclient library:\"${MYSQL_INCLUDE_DIR}\",ENABLE_MYSQL macro is turned on")
    include_directories(${MYSQL_INCLUDE_DIR})
    include_directories(${MYSQL_INCLUDE_DIR}/mysql)
    add_definitions(-DENABLE_MYSQL)
    list(APPEND LINK_LIB_LIST ${MYSQL_LIBRARIES})
endif ()

#Find out if SQLite3 is installed
find_package(SQLite3 QUIET)
if (SQLite3_FOUND AND ENABLE_SQLITE)
    message(STATUS "Find the SQLite3 library: \"${SQLite3_INCLUDE_DIR}\", ENABLE_SQLITE macro is turned on")
    include_directories(${SQLite3_INCLUDE_DIR})
    add_definitions(-DENABLE_SQLITE)
    list(APPEND LINK_LIB_LIST ${SQLite3_LIBRARIES})
endif ()

#use delete[] instead of free, it is used to solve the problem of stuck on MacOS after turning on asan
if (ASAN_USE_DELETE)
    add_definitions(-DASAN_USE_DELETE)
endif ()

#Print library files
message(STATUS "Link dependency library:${LINK_LIB_LIST}")
#Enable c++11
set(CMAKE_CXX_STANDARD 11)

if (NOT WIN32)
    add_compile_options(-Wno-deprecated-declarations)
    add_compile_options(-Wno-predefined-identifier-outside-function)
endif ()

#Compile dynamic library
if (NOT IOS AND NOT ANDROID AND NOT WIN32)
    add_library(${PROJECT_NAME}_shared SHARED ${SRC_LIST})
    target_include_directories(${PROJECT_NAME}_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${PROJECT_NAME}_shared ${LINK_LIB_LIST})
    set_target_properties(${PROJECT_NAME}_shared PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
    install(TARGETS ${PROJECT_NAME}_shared ARCHIVE DESTINATION ${INSTALL_PATH_LIB} LIBRARY DESTINATION ${INSTALL_PATH_LIB})
endif ()

#Compile static library
add_library(${PROJECT_NAME}_static STATIC ${SRC_LIST})
#Reference header file path
target_include_directories(${PROJECT_NAME}_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(${PROJECT_NAME}_static PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
#Install static library to system directory
install(TARGETS ${PROJECT_NAME}_static ARCHIVE DESTINATION ${INSTALL_PATH_LIB})

#Test program
if (NOT IOS)
    add_subdirectory(tests)
endif ()
