cmake_minimum_required(VERSION 3.16)
project(AAACompilerDevelopment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(minic_backend STATIC
    src/constant_pool.cpp
    src/diagnostic.cpp
    src/symbol_table.cpp
    src/type_rules.cpp
    src/semantic.cpp
    src/ir.cpp
    src/interpreter.cpp
    src/display.cpp
    src/compilation_result.cpp
)
target_include_directories(minic_backend PUBLIC include)
if(MSVC)
    target_compile_options(minic_backend PRIVATE /W4 /utf-8)
else()
    target_compile_options(minic_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 同学交付这两个源文件后，自动接上完整编译入口。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/lexer.cpp" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/parser.cpp")
    add_library(minic_compiler STATIC src/compiler.cpp src/lexer.cpp src/parser.cpp)
    target_link_libraries(minic_compiler PUBLIC minic_backend)
    add_executable(minic src/main.cpp)
    target_link_libraries(minic PRIVATE minic_compiler)
    add_executable(compile_and_run examples/compile_and_run.cpp)
    target_link_libraries(compile_and_run PRIVATE minic_compiler)
endif()

foreach(demo interface_demo symbol_table_demo m1_pipeline_demo m2_pipeline_demo)
    add_executable(${demo} examples/${demo}.cpp)
    target_link_libraries(${demo} PRIVATE minic_backend)
endforeach()

include(CTest)
if(BUILD_TESTING)
    foreach(test constant_pool symbol_table m1_pipeline m2_pipeline m3_pipeline)
        add_executable(${test}_test tests/${test}_test.cpp)
        target_link_libraries(${test}_test PRIVATE minic_backend)
        add_test(NAME ${test} COMMAND ${test}_test)
    endforeach()
    add_executable(compiler_flow_test tests/compiler_flow_test.cpp src/compiler.cpp)
    target_link_libraries(compiler_flow_test PRIVATE minic_backend)
    add_test(NAME compiler_flow COMMAND compiler_flow_test)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME lexer_patterns COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_lexer_patterns.py)
    endif()
endif()
