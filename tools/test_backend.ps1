param([string]$Compiler = 'g++')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    $sources = @('src/语义分析/type_arena.cpp', 'src/语义分析/type_rules.cpp', 'src/语义分析/analyzer.cpp',
        'src/符号表管理/symbol_table.cpp', 'src/中间代码生成/constant_pool.cpp',
        'src/中间代码生成/ir.cpp', 'src/中间代码生成/interpreter.cpp',
        'src/公共支持/diagnostic.cpp', 'src/总控与展示/display.cpp', 'src/总控与展示/compilation_result.cpp')
    $objects = @()
    foreach ($source in $sources) {
        $object = 'build/' + [System.IO.Path]::GetFileNameWithoutExtension($source) + '.o'
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' '-I' 'src/公共支持' '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
        $objects += $object
    }
    foreach ($name in @('constant_pool', 'symbol_table', 'm1_pipeline', 'm2_pipeline', 'm3_pipeline', 'compiler_flow')) {
        $testSources = @("tests/${name}_test.cpp")
        if ($name -eq 'compiler_flow') { $testSources += 'src/总控与展示/compiler.cpp' }
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' '-I' 'src/公共支持' @testSources @objects '-o' "build/${name}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "测试编译失败：$name" }
        & "./build/${name}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "测试失败：$name" }
    }
    foreach ($demo in @('m1_pipeline', 'm2_pipeline')) {
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' '-I' 'src/公共支持' "examples/${demo}_demo.cpp" @objects '-o' "build/${demo}_demo.exe"
        if ($LASTEXITCODE -ne 0) { throw "示例编译失败：$demo" }
        & "./build/${demo}_demo.exe"
        if ($LASTEXITCODE -ne 0) { throw "示例执行失败：$demo" }
    }
    # 总控替身测试保持隔离，正式前端对象只链接真实源码测试和命令行。
    $frontendObjects = @()
    foreach ($source in @('src/词法分析/lexer.cpp', 'src/语法分析/parser.cpp', 'src/总控与展示/compiler.cpp')) {
        $object = 'build/' + [System.IO.Path]::GetFileNameWithoutExtension($source) + '.o'
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' '-I' 'src/公共支持' '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "前端编译失败：$source" }
        $frontendObjects += $object
    }
    # m4 使用真实源码，链接正式前端；总控替身仍保持隔离。
    foreach ($name in @('m4_pipeline_test', 'type_arena_test', 'frontend_test', 'workbench_test', 'lexer_probe', 'minic', 'compile_and_run', 'workbench_driver')) {
        $source = if ($name -eq 'minic') { 'src/总控与展示/main.cpp' } elseif ($name -eq 'compile_and_run') {
            'examples/compile_and_run.cpp'
        } elseif ($name -eq 'workbench_driver') { 'tools/workbench_driver.cpp' } else { "tests/${name}.cpp" }
        $linkOptions = @()
        if ($name -in @('minic', 'workbench_driver') -and $env:OS -eq 'Windows_NT') { $linkOptions += '-municode' }
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' '-I' 'src/公共支持' $source @frontendObjects @objects @linkOptions '-o' "build/${name}.exe"
        if ($LASTEXITCODE -ne 0) { throw "前端链接失败：$name" }
    }
    & './build/m4_pipeline_test.exe'
    if ($LASTEXITCODE -ne 0) { throw '语言支持范围测试失败' }
    & './build/type_arena_test.exe'
    if ($LASTEXITCODE -ne 0) { throw '普通类型指针测试失败' }
    & './build/frontend_test.exe'
    if ($LASTEXITCODE -ne 0) { throw '前端测试失败' }
    & './build/workbench_test.exe'
    if ($LASTEXITCODE -ne 0) { throw '工作台和优化测试失败' }
    & './build/minic.exe' 'run' 'examples/frontend_demo.c'
    if ($LASTEXITCODE -ne 0) { throw '源码示例失败' }
    python tools/generate_lexer_dfa.py --check
    if ($LASTEXITCODE -ne 0) { throw 'DFA 表过期' }
    python tools/test_lexer_dfa.py build/lexer_probe.exe
    if ($LASTEXITCODE -ne 0) { throw '词法对照测试失败' }
    python tools/test_workbench_service.py build/workbench_driver.exe
    if ($LASTEXITCODE -ne 0) { throw '工作台服务测试失败' }
} finally {
    Pop-Location
}
