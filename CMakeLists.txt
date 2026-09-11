cmake_minimum_required(VERSION 3.16)
project(GigaLang VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ compiler ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ compiler version: ${CMAKE_CXX_COMPILER_VERSION}")

file(GLOB SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")

add_executable(GigaLang ${SOURCES})

target_include_directories(GigaLang PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)

find_package(LLVM 21.1 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

target_include_directories(GigaLang SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(GigaLang PRIVATE ${LLVM_DEFINITIONS})

# Map LLVM components to actual library names
llvm_map_components_to_libnames(LLVM_LIBS
    Core
    Support
    IRReader
    ExecutionEngine
    OrcJIT
    native
)

target_link_libraries(GigaLang PRIVATE ${LLVM_LIBS})

target_compile_options(GigaLang PRIVATE
    -fexceptions
    -g
)

set_target_properties(GigaLang PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target GigaLang
    COMMAND ${CMAKE_BINARY_DIR}/bin/GigaLang
    DEPENDS GigaLang
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building and running GigaLang..."
)
