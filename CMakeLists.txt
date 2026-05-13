cmake_minimum_required(VERSION 2.8)
project(flz_chat)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/flz_server)

include (flz_server/cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++11 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined")

include_directories(.)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
include_directories(/apps/flz_server/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/flz_server)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/flz_server/thirdpart)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/flz_server/thirdpart)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/flz_server/thirdpart/yaml-cpp/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/flz_server/thirdpart/jsoncpp-1.8.4/include)
link_directories(/apps/flz_server/lib)

find_package(Boost REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
endif()

#find_package(Protobuf REQUIRED)
#if(Protobuf_FOUND)
#    include_directories(${Protobuf_INCLUDE_DIRS})
#endif()
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
endif()

set(LIB_SRC
        chat/my_module.cc
    )


add_library(flz_chat SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(flz_chat)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
