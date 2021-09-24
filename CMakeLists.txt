cmake_minimum_required(VERSION 2.8)
project(mo_shell)

#set(CMAKE_C_COMPILER "gcc")
#set(CMAKE_CXX_COMPILER "g++")

set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -g -W -Wall") # 不加-O3优化，否则不好单步调试
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fgnu89-inline -g -W -Wall")

message(STATUS "CMAKE_C_COMPILER = ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER = ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_AR = ${CMAKE_AR}")
message(STATUS "CMAKE_RANLIB = ${CMAKE_RANLIB}")
message(STATUS "CMAKE_ASM_COMPILER = ${CMAKE_ASM_COMPILER}")
message(STATUS "CMAKE_LINKER = ${CMAKE_LINKER}")
message(STATUS "CMAKE_NM = ${CMAKE_NM}")
message(STATUS "CMAKE_OBJCOPY = ${CMAKE_OBJCOPY}")
message(STATUS "CMAKE_OBJDUMP = ${CMAKE_OBJDUMP}")
message(STATUS "CMAKE_STRIP = ${CMAKE_STRIP}")
message(STATUS "CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")

enable_testing()

include_directories(./src)
include_directories(./include)

aux_source_directory(./src "LIB")
add_library(libmocha STATIC ${LIB})

aux_source_directory(./tests "SHELL")
add_library(shell STATIC ${SHELL})

add_executable(${PROJECT_NAME} ${LIB} ${SHELL})

target_link_libraries(${PROJECT_NAME} m)

