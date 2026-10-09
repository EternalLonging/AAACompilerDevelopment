param([string]$Compiler = 'g++')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force build | Out-Null
    $sources = @('src/preprocessor.cpp', 'src/constant_pool.cpp', 'src/diagnostic.cpp', 'src/symbol_table.cpp',
        'src/type_rules.cpp', 'src/semantic.cpp', 'src/ir.cpp', 'src/interpreter.cpp',
        'src/display.cpp', 'src/compilation_result.cpp')
    $objects = @()
    foreach ($source in $sources) {
        $object = 'build/' + [System.IO.Path]::GetFileNameWithoutExtension($source) + '.o'
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "编译失败：$source" }
        $objects += $object
    }
    foreach ($name in @('constant_pool', 'symbol_table', 'm1_pipeline', 'm2_pipeline', 'm3_pipeline', 'm4_pipeline', 'preprocessor', 'compiler_flow')) {
        $testSources = @("tests/${name}_test.cpp")
        if ($name -eq 'compiler_flow') { $testSources += 'src/compiler.cpp' }
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' @testSources @objects '-o' "build/${name}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "测试编译失败：$name" }
        & "./build/${name}_test.exe"
        if ($LASTEXITCODE -ne 0) { throw "测试失败：$name" }
    }
    foreach ($demo in @('m1_pipeline', 'm2_pipeline')) {
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' "examples/${demo}_demo.cpp" @objects '-o' "build/${demo}_demo.exe"
        if ($LASTEXITCODE -ne 0) { throw "示例编译失败：$demo" }
        & "./build/${demo}_demo.exe"
        if ($LASTEXITCODE -ne 0) { throw "示例执行失败：$demo" }
    }
    # 总控替身测试保持隔离，正式前端对象只链接真实源码测试和命令行。
    $frontendObjects = @()
    foreach ($source in @('src/lexer.cpp', 'src/parser.cpp', 'src/compiler.cpp')) {
        $object = 'build/' + [System.IO.Path]::GetFileNameWithoutExtension($source) + '.o'
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' '-c' $source '-o' $object
        if ($LASTEXITCODE -ne 0) { throw "前端编译失败：$source" }
        $frontendObjects += $object
    }
    foreach ($name in @('frontend_test', 'lexer_probe', 'minic', 'compile_and_run')) {
        $source = if ($name -eq 'minic') { 'src/main.cpp' } elseif ($name -eq 'compile_and_run') {
            'examples/compile_and_run.cpp'
        } else { "tests/${name}.cpp" }
        & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-I' 'include' $source @frontendObjects @objects '-o' "build/${name}.exe"
        if ($LASTEXITCODE -ne 0) { throw "前端链接失败：$name" }
    }
    & './build/frontend_test.exe'
    if ($LASTEXITCODE -ne 0) { throw '前端测试失败' }
    & './build/minic.exe' 'run' 'examples/frontend_demo.c'
    if ($LASTEXITCODE -ne 0) { throw '源码示例失败' }
    python tools/generate_lexer_dfa.py --check
    if ($LASTEXITCODE -ne 0) { throw 'DFA 表过期' }
    python tools/test_lexer_dfa.py build/lexer_probe.exe
    if ($LASTEXITCODE -ne 0) { throw '词法对照测试失败' }
} finally {
    Pop-Location
}
