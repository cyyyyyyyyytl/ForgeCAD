param(
    [string]$CMakeExe = 'cmake',
    [string]$ArchivePath = "$env:USERPROFILE/vcpkg/downloads/freetype-freetype-VER-2-14-3.tar.gz"
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$workRoot = Join-Path $projectRoot 'build/local-freetype'
$sourceRoot = Join-Path $workRoot 'source'
$runtimeRoot = Join-Path $workRoot 'runtime'

# 使用与当前 vcpkg 相同的 2.14.3 源码，校验官方 port 记录的 SHA512。
if (-not (Test-Path -LiteralPath $ArchivePath)) {
    throw "找不到 FreeType 2.14.3 源码缓存，请用 -ArchivePath 指定归档：$ArchivePath"
}
$expectedHash = 'c3b6b0cc4b428c9c647ab2148386901dfd315273b68051940e8fea6010d46fdd2913467c3ef58be0d499b8e2ef5a0f1a4cc5e739756155587f4f7dff08ef9695'
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA512).Hash -ne $expectedHash) {
    throw 'FreeType 源码校验失败，停止构建。'
}
New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
Push-Location $sourceRoot
try {
    & $CMakeExe -E tar xzf $ArchivePath
    if ($LASTEXITCODE -ne 0) { throw 'FreeType 解压失败。' }
} finally { Pop-Location }

# 普通 TrueType 字体仍可使用；关闭压缩字体、PNG 字体位图等可选外部库。
& $CMakeExe -S "$sourceRoot/freetype-VER-2-14-3" -B "$workRoot/compile" `
    -G 'Visual Studio 18 2026' -A x64 -DBUILD_SHARED_LIBS=ON `
    -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_ZLIB=ON `
    -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_HARFBUZZ=ON `
    -DCMAKE_DEBUG_POSTFIX=d '-DCMAKE_C_FLAGS=/utf-8' "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$runtimeRoot"
if ($LASTEXITCODE -ne 0) { throw 'FreeType 配置失败。' }
foreach ($buildConfiguration in @('Release', 'Debug')) {
    & $CMakeExe --build "$workRoot/compile" --config $buildConfiguration --target freetype --parallel
    if ($LASTEXITCODE -ne 0) { throw "FreeType $buildConfiguration 构建失败。" }
}
Write-Host "运行库已生成：$runtimeRoot。重新配置并构建 ForgeCAD 后自动部署。"
