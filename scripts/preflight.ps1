#
# Copyright (c) Fela Ameghino 2026
#
# Distributed under the MIT License. (See accompanying file LICENSE or copy at
# https://opensource.org/licenses/MIT)
#
# Everything CI checks that can be checked on this machine, in one command.
#
#   scripts\preflight.ps1              the Windows gate: core, tests, C# binding
#   scripts\preflight.ps1 -Android     also the Android binding and the .aar
#
# The point is the round trip: a push costs ~9 minutes to discover that a shell
# script lacked its exec bit, or that a string escape needs Java 15. This runs in
# about two.
#
# Windows first, because that is what can be run here: the core and the C# binding
# are both real shipping artifacts. The Android steps need the NDK and the SDK;
# without them the script says what it skipped rather than pretending.

[CmdletBinding()]
param(
    [switch]$Android,
    [string]$BuildDir = 'build-amd64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$script:failures = @()
$script:skipped = @()

function Step {
    param([string]$Name, [scriptblock]$Body)
    Write-Host ""
    Write-Host "==> $Name" -ForegroundColor Cyan
    try {
        & $Body
        Write-Host "    ok" -ForegroundColor Green
    } catch {
        Write-Host "    FAILED: $_" -ForegroundColor Red
        $script:failures += $Name
    }
}

function Skip {
    param([string]$Name, [string]$Why)
    Write-Host ""
    Write-Host "==> $Name" -ForegroundColor DarkGray
    Write-Host "    skipped: $Why" -ForegroundColor DarkGray
    $script:skipped += "$Name ($Why)"
}

function Invoke-Native {
    param([string]$What, [scriptblock]$Body)
    $output = & $Body 2>&1
    if ($LASTEXITCODE -ne 0) {
        $output | Select-Object -Last 25 | ForEach-Object { Write-Host "    $_" }
        throw $What
    }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -prerelease -property installationPath
    if ($vsPath) {
        $candidate = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $candidate) { $vcvars = $candidate }
    }
}

# ---- Windows ---------------------------------------------------------------

Step 'JS bundle' {
    Push-Location js
    try {
        Invoke-Native 'npm run build' { npm run build }
    } finally { Pop-Location }
}

Step 'Core build + 23 tests' {
    if (-not $vcvars) { throw 'Visual Studio not found' }
    Invoke-Native 'cmake' { cmd /c "`"$vcvars`" >nul 2>&1 && cmake -B $BuildDir -S . -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build $BuildDir" }
    Invoke-Native 'ctest' { cmd /c "`"$vcvars`" >nul 2>&1 && ctest --test-dir $BuildDir --output-on-failure --timeout 300" }
}

Step 'C# binding + interop smoke' {
    # The binding Unigram ships, against the twk.dll just built — the same two
    # steps CI runs, and the only place the generated DTOs meet the real library.
    Invoke-Native 'dotnet build' { dotnet build bindings/csharp/Ton.WalletKit.csproj -c Release --nologo -v quiet }
    Invoke-Native 'dotnet publish' {
        dotnet publish bindings/csharp/test/InteropSmoke/InteropSmoke.csproj -c Release -o smoke --nologo -v quiet
    }
    Copy-Item "$BuildDir/bin/twk.dll" smoke/ -Force
    Invoke-Native 'InteropSmoke' { dotnet smoke/InteropSmoke.dll }
}

Step 'Generated code is in sync' {
    Invoke-Native 'generate-facade csharp' { node scripts/generate-facade.mjs csharp }
    Invoke-Native 'generate-facade java' { node scripts/generate-facade.mjs java }
    $dirty = git status --porcelain -- bindings/csharp/src/Facade bindings/android/src/main/java/org/ton/walletkit/core
    if ($dirty) { throw "regenerating changed files:`n$dirty" }
}

Step 'License headers' {
    Invoke-Native 'add-headers --check' { node scripts/add-headers.mjs --check }
}

Step 'Shell scripts are executable' {
    # Windows git does not track the exec bit, so a new .sh lands 100644 and CI
    # fails with "Permission denied" nine minutes later.
    foreach ($line in (git ls-files -s -- '*.sh')) {
        if ($line -notmatch '^100755') {
            throw "not executable: $line`n    fix: git update-index --chmod=+x <path>"
        }
    }
}

# ---- Android ---------------------------------------------------------------

if (-not $Android) {
    Skip 'Android binding' 'pass -Android to run it'
    Write-Host ""
} else {
    Step 'Android binding compiles at Java 8' {
        # Compiles only — no JVM harness. --release 8 is the level Gradle builds
        # the .aar at, and it is what catches language features Android will not
        # take (a `\s` escape needs Java 15 and cost a CI round trip once).
        $jni = Join-Path $root 'build-android-java'
        New-Item -ItemType Directory -Force $jni | Out-Null
        $jsonJar = Join-Path $jni 'json.jar'
        if (-not (Test-Path $jsonJar)) {
            Invoke-WebRequest -Uri 'https://repo1.maven.org/maven2/org/json/json/20240303/json-20240303.jar' -OutFile $jsonJar
        }
        Remove-Item -Recurse -Force (Join-Path $jni 'classes') -ErrorAction SilentlyContinue
        $sources = Join-Path $jni 'sources.txt'
        Get-ChildItem -Recurse bindings/android/src/main/java -Filter *.java |
            ForEach-Object { $_.FullName } | Set-Content $sources
        Invoke-Native 'javac' { javac -nowarn --release 8 -cp $jsonJar -d (Join-Path $jni 'classes') "@$sources" }

        # DtoSmoke is plain Java over org.json — no native, no JNI — so it checks
        # the generated models without an emulator or a desktop JNI build.
        Invoke-Native 'javac DtoSmoke' {
            javac -nowarn --release 8 -cp "$jsonJar;$(Join-Path $jni 'classes')" -d (Join-Path $jni 'classes') `
                bindings/android/test/DtoSmoke.java
        }
        Invoke-Native 'DtoSmoke' { java -cp "$(Join-Path $jni 'classes');$jsonJar" DtoSmoke }
    }

    $ndk = $env:ANDROID_NDK_HOME
    if (-not $ndk) { $ndk = $env:ANDROID_NDK_ROOT }

    if (-not $ndk -or -not (Test-Path $ndk)) {
        Skip 'Android cross-build' 'set ANDROID_NDK_HOME to an NDK r27 install'
    } else {
        Step 'Android cross-build' {
            # Through Git Bash: the script is sh, and the NDK toolchain files
            # work the same on Windows.
            $env:TWK_BUNDLEC = Join-Path $root "$BuildDir/bin/twk-bundlec.exe"
            $env:ANDROID_NDK_HOME = $ndk
            Invoke-Native 'android-build.sh' { bash scripts/android-build.sh }
        }
    }

    $gradle = Get-Command gradle -ErrorAction SilentlyContinue
    if (-not $gradle) {
        Skip 'Assemble the .aar' 'gradle not on PATH'
    } elseif (-not $env:ANDROID_HOME -and -not $env:ANDROID_SDK_ROOT) {
        Skip 'Assemble the .aar' 'set ANDROID_HOME to an SDK install'
    } else {
        Step 'Assemble the .aar' {
            Push-Location bindings/android
            try {
                Invoke-Native 'gradle assembleRelease' { gradle --no-daemon assembleRelease }
            } finally { Pop-Location }
        }
    }
}

Write-Host ""
if ($script:skipped.Count -gt 0) {
    Write-Host "skipped:" -ForegroundColor DarkGray
    $script:skipped | ForEach-Object { Write-Host "  - $_" -ForegroundColor DarkGray }
}
if ($script:failures.Count -gt 0) {
    Write-Host "FAILED: $($script:failures -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "preflight passed" -ForegroundColor Green
