# Building from source

See Audiokinetic's [Creating Audio Plug-ins](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=effectplugin_tools_newplugin.html) for the full toolchain setup (SDK install, environment variables, compiler requirements). Wwise offers a free personal license that includes the C++ SDK and the tools needed to build this plug-in.

## Prerequisites

- Wwise 2025.1 with the SDK (C++) and the Windows deployment platform for Visual Studio 2026 (vc180), installed through the Audiokinetic Launcher. The Windows plug-in build scripts and SDK libraries come with that deployment platform.
- Visual Studio 2026 with the C++ desktop workload for the runtime plug-in.
- For the authoring plug-in, the vc170 toolset with MFC: Visual Studio 2022, or the MSVC v143 build tools and C++ MFC for v143 in Visual Studio 2026. The Wwise 2025.1 plug-in scripts do not accept Visual Studio 2026 for the Authoring target, and the authoring plug-in uses MFC.
- Python 3, with the `markdown` and `jinja2` packages for the property help pages (`python -m pip install markdown jinja2`).

## Build

Run these commands from the `wwise` folder in a Developer Command Prompt for VS 2026, with `WWISEROOT` set to your Wwise install:

```
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" premake Windows_vc180 -t vc180
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Windows_vc180 -c Profile -x x64 -t vc180
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Windows_vc180 -c Release -x x64 -t vc180

python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" premake Authoring -t vc170
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Authoring -c Release -x x64 -t vc170

python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Documentation
```

The build copies its outputs into the Wwise install:
- Runtime plug-in: `%WWISEROOT%\SDK\x64_vc180\<Config>\lib\SpatialClusteringFX.lib` (static) and `%WWISEROOT%\SDK\x64_vc180\<Config>\bin\SpatialClustering.dll` (dynamic).
- Authoring plug-in: `%WWISEROOT%\Authoring\x64\Release\bin\Plugins\SpatialClustering.dll` and `SpatialClustering.xml`.
- Property help: `%WWISEROOT%\Authoring\Data\Plugins\SpatialClustering\Html\`, built from `wwise\WwisePlugin\res\Md`.

To also copy the runtime DLL somewhere else after each build, set `CUSTOM_WWISE_PLUGIN_DLL_PATH` for both premake and build. The DLL is then copied to `<path>\<platform>\<arch>\<toolset>\<config>\`.

## Package a release

`scripts/package.py` builds everything above (Debug, Profile and Release for the runtime plug-in) and packages it as a Wwise Launcher bundle. Run it from the repository root, in the same kind of command prompt:

```
python scripts\package.py
```

It writes `dist\SpatialClustering_<version>_Wwise<year>.<major>\` with the bundle, the license files and the changelog, and a zip of that folder for the GitHub release. `--skip-build` packages what is already built.

The plug-in version is set in `wwise/SpatialClusteringVersion.h`. The bundle version puts the Wwise version first and the plug-in version last, because the Launcher reads the first two numbers as the Wwise version the bundle is for: plug-in 3.0.0 built against Wwise 2025.1 becomes bundle version 2025.1.300.0.

## Testing without a game

The `core/tests` folder builds a standalone program around `ClusterEngine`. It does not need the Wwise SDK. It contains an assertion test suite, 12 seeded scenarios (static groups, crossing groups, one-shots, jitter, budget changes, near field, passthrough and more) and a benchmark, which also times an update that lowers Max Outputs.

```
cmake -S core/tests -B build/tests
cmake --build build/tests --config Release
cd build/tests
ctest -C Release
Release\cluster_tests bench
Release\cluster_tests list
Release\cluster_tests scenario oneshots --csv oneshots.csv
```

Run `cluster_tests` with no arguments to see all modes and options. On Linux and macOS the program is in `build/tests` instead of `build/tests/Release`.

## Testing in a game

Once the plug-in builds, you'll want a scene to exercise it in. A few options:
- **Unreal Engine 5 or Unity** with the Wwise integration.
- **[Wwise Integration Demo Sample](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=soundengine_integration_samplecode.html)**: a lightweight executable with various demos that you can build and modify yourself.
- **[Wwise sample games](https://www.audiokinetic.com/en/learning/samples/?searchToken=eyJwZ0Nfc2FtIjoiMSIsInBnU19zYW0iOiIxMDAiLCJzYW1fc3QiOiIifQ%3D%3D)** provided by Audiokinetic.

We don't yet ship a dedicated sample scene for spatial audio testing. That's on the list to add.

## Debugging

To place breakpoints and debug the plug-in, follow the steps [here](https://www.audiokinetic.com/qa/7840/wwise-sdk-how-step-through-code-using-visual-studio-debugger).
