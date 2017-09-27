cmake_minimum_required(VERSION 2.8)
project(services)
include(../../cmake/utils.cmake)

set(optServicesUseLibuv 1 CACHE BOOL "Use libuv instead of libevent")
set(optServicesBuildShared 0 CACHE BOOL "Build libservices as a shared lib, for use of the async services by several shared objects")
set(optAsanMode "" CACHE STRING "Build with AddressSanitizer, in the specified mode (-fsanitize=<mode>, i.e. address,memory) Requires GCC>= 4.9 or Clang>=3.5")

set(SRCS
  logger.cpp
  appCtx.cpp
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/..")


find_package(CURL REQUIRED)
if (optServicesUseLibuv)
    find_package(LibUV REQUIRED)
    set(SERVICES_INCLUDE_DIRS ${LibUV_INCLUDE_DIR})
    set(SERVICES_DEP_LIBS ${LibUV_LIBRARY})
    set(KR_USE_LIBUV 1)
    set(KR_USE_LIBWEBSOCKETS 1)
    list(APPEND SRCS
        waiter/libuvWaiter.cpp
    )
else()
    find_package(LibEvent REQUIRED)
    add_subdirectory(${LIBWS_DIR} libws)
    set(SERVICES_INCLUDE_DIRS ${LIBEVENT_INCLUDE_DIRS} ${LIBWS_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/libws)
    set(SERVICES_DEP_LIBS {LIBEVENT_LIBRARIES})
    list(APPEND SRCS
        waiter/libeventWaiter.cpp
    )
    set(LIBWS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third-party/libws CACHE PATH "libws source dir, where its CMakeLists.txt is located")
    set(LIBWS_EXTERNAL_LOOP 1 CACHE BOOL "Use libws with external libevent loop")
endif()

list(APPEND SERVICES_INCLUDE_DIRS
     ${CURL_INCLUDE_DIRS}
     ${CMAKE_CURRENT_SOURCE_DIR}
     ${CMAKE_CURRENT_BINARY_DIR}
)

set_property(GLOBAL PROPERTY SERVICES_INCLUDE_DIRS ${SERVICES_INCLUDE_DIRS})
include_directories(${SERVICES_INCLUDE_DIRS})

if (("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang") OR ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU"))
    add_definitions(-fvisibility=hidden -fPIC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

    if (optAsanMode AND (${CMAKE_BUILD_TYPE} STREQUAL "Debug"))
        add_definitions(-fsanitize=${optAsanMode} -fno-omit-frame-pointer)
        if (optServicesBuildShared)
            set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=${optAsanMode}")
        endif()
    endif()
endif()

if (WIN32)
#    set(SYSLIBS zlib)
else()
    set(SYSLIBS z)
    if (NOT APPLE)
        list(APPEND SYSLIBS pthread)
    endif()
endif()

if (CLANG_STDLIB)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=lib${CLANG_STDLIB}")
    list(APPEND SYSLIBS ${CLANG_STDLIB})
endif()

if (WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS /wd4800 /wd4996)
endif()

if (optServicesBuildShared)
    add_definitions(-DMEGAIO_DLL)
    add_library(services SHARED ${SRCS})
else()
    add_library(services ${SRCS})
endif()

list(APPEND SERVICES_DEP_LIBS
  ${CURL_LIBRARIES}
  ${SYSLIBS}
)
configure_file(services-config.h.in services-config.h @ONLY)
target_link_libraries(services ${SERVICES_DEP_LIBS})
