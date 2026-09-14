<# :
@echo off
:: Check for Administrator privileges
net session >nul 2>&1
if %errorLevel% == 0 (
    goto :admin
) else (
    echo Requesting administrative privileges...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

:admin
title Webconventer USB Build Tool
chcp 65001 >nul
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "iex ((Get-Content -LiteralPath '%~f0' -Raw))"
exit /b %ERRORLEVEL%
#>

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ESC     = [char]27
$CYAN    = "$ESC[96m"
$YELLOW  = "$ESC[93m"
$GREEN   = "$ESC[92m"
$RED     = "$ESC[91m"
$RESET   = "$ESC[0m"
$PINK    = "$ESC[95m"
$WHITE   = "$ESC[97m"
$GRAY    = "$ESC[90m"

$NAV_ARROW = [char]0x2195

$options = @(
    "Compile Release (x64, GUI Only)",
    "Compile Debug (x64, Verbose Console)",
    "Clean Build Files"
)
$selected = 0

function Draw-Menu {
    param (
        [string[]]$Options,
        [int]$Selected
    )
    [Console]::SetCursorPosition(0, 0)
    
    Write-Host ""
    Write-Host "$CYAN  Webconventer USB Build Tool$RESET"
    Write-Host "$GRAY  FAT32 <-> EXT4 Smart USB Converter$RESET"
    Write-Host ""
    Write-Host "$PINK  $NAV_ARROW = nav (or W/S), ENTER = ok, ESC = exit$RESET"
    Write-Host ""
    
    for ($i = 0; $i -lt $Options.Length; $i++) {
        $pad = " " * 4
        if ($i -eq $Selected) {
            Write-Host "$pad$WHITE >  $($Options[$i])$RESET"
        } else {
            Write-Host "$pad$GRAY     $($Options[$i])$RESET"
        }
    }
    Write-Host ""
}

function Show-Logs {
    param (
        [string]$Status,
        [string]$Color,
        [string]$Message
    )
    $time = Get-Date -Format "HH:mm:ss"
    Write-Host -NoNewline "$GRAY[$WHITE $time $GRAY] $WHITE[$RESET"
    Write-Host -NoNewline "$Color$($Status.PadRight(4))$RESET"
    Write-Host "$WHITE]$RESET $Message"
}

function Invoke-BuildStep {
    param (
        [string]$StepName,
        [scriptblock]$CommandBlock
    )
    Show-Logs "STEP" $YELLOW "Starting: $StepName"
    
    $output = [System.Collections.Generic.List[string]]::new()
    try {
        & $CommandBlock 2>&1 | ForEach-Object {
            $output.Add($_)
        }
        
        if ($LASTEXITCODE -ne 0) {
            throw "Process exited with code $LASTEXITCODE"
        }
        Show-Logs "DONE" $GREEN "Finished: $StepName"
        return $true
    } catch {
        Show-Logs "FAIL" $RED "Error during step: $StepName"
        if ($output.Count -gt 0) {
            Write-Host "`n$RED--- [ Diagnostics Output ] ---$RESET"
            foreach ($line in $output) {
                Write-Host "  $line"
            }
            Write-Host "$RED--------------------------------------------------$RESET`n"
        }
        return $false
    }
}

function Execute-BuildPipeline {
    param (
        [string]$Config
    )
    
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        Show-Logs "ERR" $RED "CMake is not found in the system PATH."
        return
    }

    Clear-Host
    Write-Host "`n$CYAN  Build Pipeline [$Config]$RESET`n"

    $packed = Invoke-BuildStep "Embedding frontend resources into C++" {
        & powershell -ExecutionPolicy Bypass -File src/pack_resources.ps1
    }
    if (-not $packed) { return }

    $clangC   = "C:/Program Files/LLVM/bin/clang.exe"
    $clangCXX = "C:/Program Files/LLVM/bin/clang++.exe"
    $clangRC  = "C:/Program Files/LLVM/bin/llvm-rc.exe"

    $useClang = (Test-Path $clangC) -and (Test-Path $clangCXX)

    $configured = Invoke-BuildStep "Configuring CMake build environment" {
        $cmakeArgs = @("-S", ".", "-B", "temp")
        if ($useClang) {
            $cmakeArgs += @(
                "-G", "Ninja",
                "-DCMAKE_CXX_COMPILER=$clangCXX",
                "-DCMAKE_C_COMPILER=$clangC",
                "-DCMAKE_RC_COMPILER=$clangRC"
            )
        }
        $cmakeArgs += ("-DCMAKE_BUILD_TYPE=" + $Config)
        & cmake @cmakeArgs
    }
    if (-not $configured) { return }

    $built = Invoke-BuildStep "Compiling Webconventer USB ($Config)" {
        & cmake --build temp --config $Config
    }
    if (-not $built) { return }

    if ($Config -eq "Release") {
        Show-Logs "DONE" $GREEN "Binary created: builds/webconventer-usb.exe"
    } else {
        Show-Logs "DONE" $GREEN "Binary created: builds/webconventer-usb-debug.exe (Console enabled)"
    }
}

function Clear-BuildDirectories {
    Clear-Host
    Write-Host "`n$CYAN  Cleaning Build Environment$RESET`n"
    
    $deleted = $false
    $targets = @("temp", "temp_debug", "builds")
    
    foreach ($target in $targets) {
        if (Test-Path $target) {
            Show-Logs "INFO" $RED "Removing directory: $target..."
            try {
                Remove-Item $target -Recurse -Force -ErrorAction Stop
                $deleted = $true
            } catch {
                Show-Logs "WARN" $YELLOW "Could not completely remove '$target'. The directory or a file may be locked."
            }
        }
    }
    
    if ($deleted) {
        Show-Logs "DONE" $GREEN "Cleanup completed."
    } else {
        Show-Logs "DONE" $GREEN "Directories are already clean. No action required."
    }
}

try {
    [Console]::CursorVisible = $false
    Clear-Host

    while ($true) {
        Draw-Menu -Options $options -Selected $selected
        
        $key = [System.Console]::ReadKey($true)
        if ($key.Key -eq "UpArrow" -or $key.KeyChar -eq 'w' -or $key.KeyChar -eq 'W') {
            $selected = ($selected - 1 + $options.Length) % $options.Length
        } elseif ($key.Key -eq "DownArrow" -or $key.KeyChar -eq 's' -or $key.KeyChar -eq 'S') {
            $selected = ($selected + 1) % $options.Length
        } elseif ($key.Key -eq "Escape" -or $key.KeyChar -eq 'q' -or $key.KeyChar -eq 'Q') {
            break
        } elseif ($key.Key -eq "Enter") {
            if ($selected -eq 0) {
                Execute-BuildPipeline -Config "Release"
            } elseif ($selected -eq 1) {
                Execute-BuildPipeline -Config "Debug"
            } elseif ($selected -eq 2) {
                Clear-BuildDirectories
            }
            
            Write-Host "`n$GRAY  Press any key to return to menu...$RESET"
            [System.Console]::ReadKey($true) > $null
            Clear-Host
        }
    }
} finally {
    [Console]::CursorVisible = $true
    Clear-Host
}
