param(
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$target = Join-Path $PSScriptRoot "chirshell"

if ($Clean) {
    Write-Host "Cleaning build artifacts..."
    if (Test-Path "$target.exe") { Remove-Item "$target.exe" -Force }
    if (Test-Path $target) { Remove-Item $target -Force }
    Get-ChildItem -Path $PSScriptRoot -Filter "*.o" | Remove-Item -Force -ErrorAction SilentlyContinue
    return
}

$sources = @(
    "src/main.cpp",
    "src/parser.cpp",
    "src/executor.cpp",
    "src/builtins.cpp",
    "src/jobs.cpp",
    "src/signals.cpp",
    "src/prompt.cpp"
) | ForEach-Object { Join-Path $PSScriptRoot $_ }

$includeDir = Join-Path $PSScriptRoot "include"
$argv = @(
    "g++",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-pthread",
    "-I", $includeDir
) + $sources + @("-o", $target, "-pthread")

Write-Host "Compiling chirshell..."
Write-Host ($argv -join " ")

$process = Start-Process -FilePath $argv[0] -ArgumentList $argv[1..($argv.Length - 1)] -NoNewWindow -PassThru -Wait
if ($process.ExitCode -ne 0) {
    throw "Compilation failed with exit code $($process.ExitCode)"
}

Write-Host "Build complete: $target"



