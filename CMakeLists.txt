cmake_minimum_required(VERSION 3.16)
project(AAACompilerDevelopment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# MinGW 汇编器不能写入中文目标路径，构建副本使用英文临时路径。
# configure_file 会跟踪源码变化，重新构建时自动更新副本。
function(minic_sources output)
    set(result)
    foreach(source IN LISTS ARGN)
        if(MINGW)
            get_filename_component(filename "${source}" NAME)
            set(compilation_source "${CMAKE_CURRENT_BINARY_DIR}/native_sources/${filename}")
            configure_file("${source}" "${compilation_source}" COPYONLY)
            list(APPEND result "${compilation_source}")
        else()
            list(APPEND result "${source}")
        endif()
    endforeach()
    set(${output} "${result}" PARENT_SCOPE)
endfunction()

minic_sources(backend_sources
    src/语义分析/type_arena.cpp
    src/语义分析/type_rules.cpp
    src/语义分析/analyzer.cpp
    src/符号表管理/symbol_table.cpp
    src/中间代码生成/constant_pool.cpp
    src/中间代码生成/ir.cpp
    src/中间代码生成/interpreter.cpp
    src/公共支持/diagnostic.cpp
    src/总控与展示/display.cpp
    src/总控与展示/compilation_result.cpp
)
add_library(minic_backend STATIC ${backend_sources})
target_include_directories(minic_backend PUBLIC include PRIVATE src/公共支持)
if(MSVC)
    target_compile_options(minic_backend PRIVATE /W4 /utf-8)
else()
    target_compile_options(minic_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 词法、语法与后端组成真实源码编译入口。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/词法分析/lexer.cpp")
    minic_sources(compiler_sources
        src/总控与展示/compiler.cpp
        src/词法分析/lexer.cpp
        src/语法分析/parser.cpp
    )
    add_library(minic_compiler STATIC ${compiler_sources})
    target_include_directories(minic_compiler PRIVATE src/词法分析)
    target_link_libraries(minic_compiler PUBLIC minic_backend)
    if(MSVC)
        target_compile_options(minic_compiler PRIVATE /W4 /utf-8)
    else()
        target_compile_options(minic_compiler PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    minic_sources(cli_sources src/总控与展示/main.cpp)
    add_executable(minic ${cli_sources})
    target_link_libraries(minic PRIVATE minic_compiler)
    if(MINGW)
        target_link_options(minic PRIVATE -municode)
    endif()
    add_executable(compile_and_run examples/compile_and_run.cpp)
    target_link_libraries(compile_and_run PRIVATE minic_compiler)
    add_executable(workbench_driver tools/workbench_driver.cpp)
    target_link_libraries(workbench_driver PRIVATE minic_compiler)
    if(MINGW)
        target_link_options(workbench_driver PRIVATE -municode)
    endif()
endif()

foreach(demo interface_demo symbol_table_demo m1_pipeline_demo m2_pipeline_demo)
    add_executable(${demo} examples/${demo}.cpp)
    target_link_libraries(${demo} PRIVATE minic_backend)
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(arena_lifetime_test tests/arena_lifetime_test.cpp)
    target_link_libraries(arena_lifetime_test PRIVATE minic_backend)
    add_test(NAME arena_lifetime COMMAND arena_lifetime_test)
    add_executable(type_arena_test tests/type_arena_test.cpp)
    target_link_libraries(type_arena_test PRIVATE minic_compiler)
    add_test(NAME type_arena COMMAND type_arena_test)
    add_executable(frontend_test tests/frontend_test.cpp)
    target_link_libraries(frontend_test PRIVATE minic_compiler)
    add_test(NAME frontend COMMAND frontend_test)
    add_executable(workbench_test tests/workbench_test.cpp)
    target_link_libraries(workbench_test PRIVATE minic_compiler)
    add_test(NAME workbench COMMAND workbench_test)
    add_executable(lexer_probe tests/lexer_probe.cpp)
    target_link_libraries(lexer_probe PRIVATE minic_compiler)
    foreach(test constant_pool symbol_table m1_pipeline m2_pipeline m3_pipeline m4_pipeline)
        add_executable(${test}_test tests/${test}_test.cpp)
        target_link_libraries(${test}_test PRIVATE minic_compiler)
        add_test(NAME ${test} COMMAND ${test}_test)
    endforeach()
    add_executable(compiler_flow_test tests/compiler_flow_test.cpp)
    target_link_libraries(compiler_flow_test PRIVATE minic_compiler)
    add_test(NAME compiler_flow COMMAND compiler_flow_test)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME lexer_differential COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_lexer_dfa.py $<TARGET_FILE:lexer_probe>)
        add_test(NAME lexer_dfa_current COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_lexer_dfa.py --check)
        add_test(NAME lexer_tables_current COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_lexer_tables.py --check)
        add_test(NAME lexer_patterns COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_lexer_patterns.py)
        if(TARGET minic_compiler)
            add_executable(integration_driver tests/integration_driver.cpp)
            target_link_libraries(integration_driver PRIVATE minic_compiler)
            if(MINGW)
                target_link_options(integration_driver PRIVATE -municode)
            endif()
            add_test(NAME source_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_integration.py
                     --compiler $<TARGET_FILE:integration_driver>)
            add_test(NAME cli_acceptance COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_acceptance.py
                     $<TARGET_FILE:minic>)
            add_test(NAME showcase COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_showcase.py
                     --compiler $<TARGET_FILE:minic> --output ${CMAKE_CURRENT_BINARY_DIR}/showcase)
            add_test(NAME workbench_service COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_workbench_service.py
                     $<TARGET_FILE:workbench_driver>)
        endif()
    endif()
endif()
