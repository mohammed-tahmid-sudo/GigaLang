cmake_minimum_required(VERSION 3.10)
project(GigaLang VERSION 0.1 LANGUAGES CXX C)

# Use modern CMake to require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Enable Compile Commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable debug symbols
set(CMAKE_BUILD_TYPE Debug)

# Source files
file(GLOB SOURCES "src/*.cpp")

# Create executable
add_executable(GigaLang ${SOURCES})

# Include project headers
target_include_directories(GigaLang PRIVATE "${PROJECT_SOURCE_DIR}/include")

# --- LLVM Integration ---
find_package(LLVM 18.1.3 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")

# Include LLVM headers and definitions
target_include_directories(GigaLang PRIVATE ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Get compile flags from llvm-config
execute_process(
    COMMAND llvm-config-18 --cxxflags
    OUTPUT_VARIABLE LLVM_CXX_FLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX REPLACE " +" ";" LLVM_CXX_FLAGS_LIST "${LLVM_CXX_FLAGS}")
target_compile_options(GigaLang PRIVATE ${LLVM_CXX_FLAGS_LIST} -g)

# Get link flags from llvm-config
execute_process(
    COMMAND llvm-config-18 --ldflags
    OUTPUT_VARIABLE LLVM_LD_FLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX REPLACE " +" ";" LLVM_LD_FLAGS_LIST "${LLVM_LD_FLAGS}")

# Get LLVM libraries (only commonly used components for compiler projects)
execute_process(
    COMMAND llvm-config-18 --libs core IRReader ExecutionEngine Support
    OUTPUT_VARIABLE LLVM_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX REPLACE " +" ";" LLVM_LIBS_LIST "${LLVM_LIBS}")

# Get system libraries
execute_process(
    COMMAND llvm-config-18 --system-libs
    OUTPUT_VARIABLE LLVM_SYSTEM_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX REPLACE " +" ";" LLVM_SYSTEM_LIBS_LIST "${LLVM_SYSTEM_LIBS}")

# Link everything
target_link_libraries(GigaLang PRIVATE ${LLVM_LD_FLAGS_LIST} ${LLVM_LIBS_LIST} ${LLVM_SYSTEM_LIBS_LIST})

# Output directory
set_target_properties(GigaLang PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")
target_compile_options(GigaLang PRIVATE -fexceptions)

# Custom run target
add_custom_target(run
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target GigaLang
    COMMAND ${CMAKE_BINARY_DIR}/bin/GigaLang
    DEPENDS GigaLang
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building and running GigaLang..."
)

