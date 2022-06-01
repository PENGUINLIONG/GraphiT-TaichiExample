param(
    [string] $AppName,
    [switch] $Verbose
)

if (-not $AppName) {
    $AppName = "GraphiT-Template"
}

$CmdArgs = ""

if ($Verbose) {
    $CmdArgs += "-v"
}

if (-not(Test-Path "build-host")) {
    New-Item "build-host" -ItemType Directory
}

Push-Location "build-host"
cmake ..
cmake --build . -t $AppName
Pop-Location

if ($lastexitcode -ne 0) {
    exit
}

& ./build-host/bin/Debug/$AppName.exe $CmdArgs
