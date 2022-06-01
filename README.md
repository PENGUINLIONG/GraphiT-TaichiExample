# GraphiT-Taichi Interop Example

This repository showcases how a foreign application integrate Taichi Core with
its new ahead-of-time (AOT) compiled modules.

## Compile Ahead-of-Time

To compile the fractal shaders ahead-of-time:

```bash
python ./scripts/fractal.py
```

## Building Example App

On Windows, you can run a handy script to build the example:

```powershell
./Run-Host.ps1 [-BuildTaichiFromScratch]
```

Without specifying `-BuildTaichiFromScratch` prebuilt binary,
`prebuilt/x86_64-pc-msvc-windows` is linked; otherwise a fresh build of
`taichi_c_api` will be used. You need to checkout `taichi` to
[this branch](https://github.com/PENGUINLIONG/taichi/tree/c-api) for a
successful build.

The example at the moment doesn't support other platforms than windows,
basically because the library I used can only create windows on Windows.
