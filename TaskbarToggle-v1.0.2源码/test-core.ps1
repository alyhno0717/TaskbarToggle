# Opt-in integration test: briefly toggles the real desktop taskbar, uses a
# dedicated blank Edge profile, restores desktop state, and stops that browser.
param([string]$Toolchain = (Join-Path (Split-Path -Parent $PSScriptRoot) '开发工具\llvm-mingw-20260922-ucrt-x86_64'))
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
$edge = $null
$stubborn = $null
$browserPid = 0
try {
    New-Item -ItemType Directory -Path build -Force | Out-Null
    $clang = Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe'
    & $clang -std=c++20 -O2 -Wall -Wextra -static -municode -D_WIN32_WINNT=0x0A00 tests/core-integration.cpp -o build/core-integration.exe -luser32 -lshell32 -ladvapi32 -lcomctl32 -lgdi32 -lole32 -luuid
    if ($LASTEXITCODE -ne 0) { throw 'Core integration compilation failed.' }
    & $clang -std=c++20 -O2 -Wall -Wextra -static -municode -mwindows -D_WIN32_WINNT=0x0A00 tests/stubborn-window.cpp -o build/stubborn-window.exe -luser32
    if ($LASTEXITCODE -ne 0) { throw 'Independent test window compilation failed.' }
    $edgePath = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
    if (!(Test-Path -LiteralPath $edgePath)) { $edgePath = 'C:\Program Files\Microsoft\Edge\Application\msedge.exe' }
    $profile = Join-Path $PSScriptRoot 'build\edge-core-profile'
    $arguments = '--user-data-dir="' + $profile + '" --no-first-run --no-default-browser-check --disable-background-mode --disable-background-networking --disable-sync --start-maximized --new-window about:blank'
    $edge = Start-Process -FilePath $edgePath -ArgumentList $arguments -WindowStyle Hidden -PassThru
    [void]$edge.WaitForInputIdle(10000)
    Start-Sleep -Seconds 4
    $browser = @(Get-CimInstance Win32_Process -Filter "Name='msedge.exe'" | Where-Object {
        $_.CommandLine -like '*edge-core-profile*' -and $_.CommandLine -notmatch '--type='
    })
    if ($browser.Count -ne 1) { throw 'Dedicated test browser process was not uniquely found.' }
    $browserPid = $browser[0].ProcessId
    $stubborn = Start-Process -FilePath (Join-Path $PSScriptRoot 'build\stubborn-window.exe') -WindowStyle Hidden -PassThru
    [void]$stubborn.WaitForInputIdle(10000)
    & .\build\core-integration.exe $browserPid $stubborn.Id | Tee-Object -FilePath build/core-results.txt
    if ($LASTEXITCODE -ne 0) { throw 'Core integration failed.' }
} finally {
    if ($browserPid -ne 0) { Stop-Process -Id $browserPid -ErrorAction SilentlyContinue }
    if ($edge) {
        $edge.Refresh()
        if (!$edge.HasExited) { Stop-Process -Id $edge.Id -ErrorAction SilentlyContinue }
    }
    if ($stubborn) {
        $stubborn.Refresh()
        if (!$stubborn.HasExited) { Stop-Process -Id $stubborn.Id -ErrorAction SilentlyContinue }
    }
    Pop-Location
}
