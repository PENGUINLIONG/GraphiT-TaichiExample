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

if (-not(Test-Path "build-android-aarch64")) {
    New-Item "build-android-aarch64" -ItemType Directory
}

Push-Location "build-android-aarch64"
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE="$env:ANDROID_NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI="arm64-v8a" -DANDROID_PLATFORM=android-29 -G "Ninja" ..
cmake --build . -t $AppName 
Pop-Location

if ($lastexitcode -ne 0) {
    exit
}

adb shell mkdir /data/local/tmp/$AppName
adb shell mkdir /data/local/tmp/$AppName/asset
adb shell mkdir /data/local/tmp/$AppName/bin
adb push ./build-android-aarch64/assets /data/local/tmp/$AppName/
adb push ./build-android-aarch64/bin /data/local/tmp/$AppName/
adb shell chmod 777 /data/local/tmp/$AppName/bin/$AppName
adb shell "cd /data/local/tmp/$AppName/bin && ./$AppName $CmdArgs"
