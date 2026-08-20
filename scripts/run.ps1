# 快速构建并运行 ForgeCAD（Windows + MSVC）
# 用法： powershell -ExecutionPolicy Bypass -File scripts\run.ps1

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$toolchain = "C:\Users\15389\vcpkg\scripts\buildsystems\vcpkg.cmake"
$qtPrefix = "D:\Qt\6.11.2\msvc2022_64"

# 若未配置则配置
if (-not (Test-Path "build\CMakeCache.txt")) {
    & $cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T v145 `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DCMAKE_PREFIX_PATH=$qtPrefix"
}

& $cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { throw "构建失败" }

# 运行 GUI（确保能找到 Qt DLL）
$env:PATH = "$qtPrefix\bin;" + $env:PATH
Start-Process (Join-Path $PWD "build\bin\Release\forgecad.exe")
