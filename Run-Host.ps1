param(
    [switch] $BuildTaichiFromScratch
)

if ($BuildTaichiFromScratch) {
    $env:BUILD_TAICHI_FROM_SCRATCH = 1
} else {
    $env:BUILD_TAICHI_FROM_SCRATCH = 0
}

if (-not(Test-Path "build-host")) {
    New-Item "build-host" -ItemType Directory
}

Push-Location "build-host"
cmake ..
cmake --build . -t GraphiT-TaichiExample --config Release
Pop-Location

if ($lastexitcode -ne 0) {
    exit
}

& ./build-host/bin/GraphiT-TaichiExample.exe -m ./assets
