param(
    [string]$Toolchain = (Join-Path (Split-Path -Parent $PSScriptRoot) '开发工具\llvm-mingw-20260922-ucrt-x86_64'),
    [ValidateSet('LLVM', 'MSVC')][string]$Compiler = 'LLVM'
)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path 'build' -Force | Out-Null
    New-Item -ItemType Directory -Path 'dist' -Force | Out-Null
    if ($Compiler -eq 'MSVC') {
        # Run this branch from an x64 Native Tools PowerShell / Developer PowerShell.
        & rc.exe /nologo /fo build/resource.res resource.rc
        if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
        & cl.exe /nologo /std:c++20 /utf-8 /W4 /O2 /MT /EHsc /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /Fo:build/main.obj main.cpp build/resource.res /Fe:dist/TaskbarToggle.exe /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /DYNAMICBASE /NXCOMPAT
        if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
    } else {
        $bin = Join-Path $Toolchain 'bin'
        $windres = Join-Path $bin 'llvm-windres.exe'
        $clang = Join-Path $bin 'x86_64-w64-mingw32-clang++.exe'
        if (!(Test-Path -LiteralPath $clang)) { throw "Compiler not found: $clang" }
        & $windres -i resource.rc -o build/resource.o -O coff
        if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
        & $clang -std=c++20 -O2 -Wall -Wextra -Wpedantic -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -municode -mwindows -static -ffunction-sections -fdata-sections main.cpp build/resource.o -o dist/TaskbarToggle.exe -luser32 -lshell32 -ladvapi32 -lcomctl32 -lgdi32 -lole32 -luuid '-Wl,--gc-sections' '-Wl,--dynamicbase' '-Wl,--nxcompat' '-Wl,--strip-all'
        if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
    }
    Get-Item -LiteralPath 'dist/TaskbarToggle.exe' | Select-Object FullName,Length
    Get-FileHash -LiteralPath 'dist/TaskbarToggle.exe' -Algorithm SHA256 | Format-List
} finally {
    Pop-Location
}
