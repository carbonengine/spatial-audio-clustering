# Building from Source

See Audiokinetic's [Creating Audio Plug-ins](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=effectplugin_tools_newplugin.html) for the full toolchain setup. Wwise offers a free personal license that includes the C++ SDK and the tools needed to build this plug-in.

## Prerequisites

- Wwise 2025.1 with the SDK (C++) and the Windows deployment platform for Visual Studio 2026 (vc180), installed through the Audiokinetic Launcher. The Windows plug-in build scripts and SDK libraries come with that deployment platform.
- Visual Studio 2026 with the C++ desktop workload for the runtime plug-in, and the Windows 11 SDK 10.0.22621.0, an individual component in the Visual Studio Installer. The Wwise 2025.1 build scripts target that SDK version, and the build fails if only a newer SDK is installed.
- For the authoring plug-in, the vc170 toolset with MFC: Visual Studio 2022, or the MSVC v143 build tools and C++ MFC for v143 in Visual Studio 2026. The Wwise 2025.1 plug-in scripts do not accept Visual Studio 2026 for the Authoring target.
- Python 3, with the `markdown` and `jinja2` packages for the property help pages: `python -m pip install markdown jinja2`.

## Build

Run these commands from the `wwise` folder in a Developer Command Prompt for VS 2026, with `WWISEROOT` set to your Wwise install:

```
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" premake Windows_vc180 -t vc180
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Windows_vc180 -c Release -x x64 -t vc180

python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" premake Authoring -t vc170
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Authoring -c Release -x x64 -t vc170

python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Documentation
```

Repeat the `build Windows_vc180` line with `-c Debug` and `-c Profile` for the other runtime configurations.

With only Visual Studio 2026 installed, `wp.py build Authoring` stops with "Could not find the Visual Studio Build Tools for vc170", because it looks for a Visual Studio 2022 installation to find MSBuild. The v143 toolset in Visual Studio 2026 builds the plug-in fine; run MSBuild on the solution that premake generated instead. `scripts/package.py` does this for you.

```
msbuild SpatialClustering_Authoring_Windows_vc170.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

The build copies its outputs into the Wwise install:
- Runtime plug-in: `%WWISEROOT%\SDK\x64_vc180\<Config>\lib\SpatialClusteringFX.lib` for static linking, and `%WWISEROOT%\SDK\x64_vc180\<Config>\bin\SpatialClustering.dll` with its `.pdb` for dynamic loading.
- Factory header: `%WWISEROOT%\SDK\include\AK\Plugin\SpatialClusteringFXFactory.h`, copied by the authoring build.
- Authoring plug-in: `%WWISEROOT%\Authoring\x64\Release\bin\Plugins\SpatialClustering.dll` and `SpatialClustering.xml`.
- Property help: `%WWISEROOT%\Authoring\Data\Plugins\SpatialClustering\Html\`, built from `wwise\WwisePlugin\res\Md`.

To also copy the runtime DLL somewhere else after each build, set `CUSTOM_WWISE_PLUGIN_DLL_PATH` for both premake and build. The DLL is then copied to `<path>\<platform>\<arch>\<toolset>\<config>\`, for example `<path>\Windows\x64\v145\Release\`.

## Build with CMake

The root `CMakeLists.txt` builds the runtime plug-in as a static library and installs it as the `SpatialClustering` CMake package, for games that build their Wwise plug-ins from source. It uses a `Wwise::Wwise` target when one exists or a `Wwise` CMake package provides it, such as a vcpkg port. Otherwise it uses the SDK folder in `WWISE_SDK_DIR`, which defaults to the `WWISESDK` environment variable, and defines `AK_OPTIMIZED` in the Release configuration like the Release sound engine libraries.

The library has no debug suffix, so install Debug and Release builds to separate prefixes.

A game can also add the repository to its own build with `add_subdirectory`, for example as a git subtree, and link `SpatialClustering::SpatialClusteringFX`. The plug-in then builds with the game's configurations and its `Wwise::Wwise` target, and generates no install rules unless `SPATIALCLUSTERING_INSTALL` is on.

With `-DSPATIALCLUSTERING_BUILD_AUTHORING=ON` it also builds the authoring plug-in, on Windows only. The `Authoring` install component copies it into a Wwise install with the same layout as the Launcher, which is useful for a Wwise install kept in source control: the plug-in is built against that install's exact SDK. Use the vc170 toolset, `-T v143`, because Wwise Authoring ships the Visual Studio 2022 runtime. The Visual Studio 2026 generator needs CMake 4.2 or later, such as the one that comes with Visual Studio 2026.

```
cmake -S . -B build/authoring -G "Visual Studio 18 2026" -A x64 -T v143 -DSPATIALCLUSTERING_BUILD_AUTHORING=ON -DWWISE_SDK_DIR=<Wwise SDK folder>
cmake --build build/authoring --config Release
cmake --install build/authoring --config Release --component Authoring --prefix <Wwise install>
```

The install copies `SpatialClustering.dll`, `.pdb` and `.xml` to `Authoring\x64\Release\bin\Plugins`, and the property help to `Authoring\Data\Plugins\SpatialClustering\Html`. Generate the help with `wp.py build Documentation` before configuring, since CMake only picks it up at configure time.

## Package a Release

`scripts/package.py` builds the runtime plug-in in Debug, Profile and Release, the authoring plug-in and the help, and packages them as a Wwise Launcher bundle. Run it from the repository root, in the same kind of command prompt:

```
python scripts\package.py
```

It packages what is in the Wwise install that `WWISEROOT` points to, and writes `dist\SpatialClustering_<version>_Wwise<year>.<major>\` with the bundle and the license files, plus a zip of that folder for the GitHub release. `--skip-build` packages what is already built, without building the help either.

The plug-in version is set in `wwise/SpatialClusteringVersion.h`. Update both the major, minor and patch numbers and the version string: `package.py` reads the numbers and CMake reads the string. The bundle version puts the Wwise version first and the plug-in version last, because the Launcher reads the first two numbers as the Wwise version the bundle is for: plug-in 3.0.0 built against Wwise 2025.1 becomes bundle version 2025.1.300.0. The minor version must stay below 100.

## Testing in a Game

Once the plug-in builds, you'll want a scene to exercise it in. A few options:
- **Unreal Engine 5 or Unity** with the Wwise integration.
- **[Wwise Integration Demo Sample](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=soundengine_integration_samplecode.html)**: a lightweight executable with various demos that you can build and modify yourself.
- **[Wwise sample games](https://www.audiokinetic.com/en/learning/samples/?searchToken=eyJwZ0Nfc2FtIjoiMSIsInBnU19zYW0iOiIxMDAiLCJzYW1fc3QiOiIifQ%3D%3D)** provided by Audiokinetic.

We don't ship a dedicated sample scene for spatial audio testing yet.

## Debugging

To place breakpoints and debug the plug-in, follow [these steps](https://www.audiokinetic.com/qa/7840/wwise-sdk-how-step-through-code-using-visual-studio-debugger).
