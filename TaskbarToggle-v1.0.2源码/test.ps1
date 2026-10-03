param([string]$Toolchain = (Join-Path (Split-Path -Parent $PSScriptRoot) '开发工具\llvm-mingw-20260922-ucrt-x86_64'))
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    $clang = Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe'
    & $clang -std=c++20 -O2 -Wall -Wextra -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -municode -static tests/integration.cpp -o build/integration-tests.exe -luser32 -lshell32 -ladvapi32 -lcomctl32 -lgdi32 -lole32 -luuid
    if ($LASTEXITCODE -ne 0) { throw 'Integration test compilation failed.' }
    & .\build\integration-tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Integration tests failed.' }
} finally { Pop-Location }
