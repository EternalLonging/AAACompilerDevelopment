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
} finally {
    Pop-Location
}
