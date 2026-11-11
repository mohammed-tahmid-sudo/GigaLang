cmake_minimum_required(VERSION 4.2.3)
project(GigaLang)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

file(GLOB SRCS src/*.cpp)

file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS 
    "${CMAKE_SOURCE_DIR}/include/*.h"
)
include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(GigaLang ${SRCS})

llvm_map_components_to_libnames(LLVM_LIBS
    Core
    Support
)

target_link_libraries(GigaLang PRIVATE ${LLVM_LIBS})

# target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
