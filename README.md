# Spatial Clustering for Wwise

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE.txt)
[![Wwise](https://img.shields.io/badge/Wwise-2025.1-green.svg)](https://www.audiokinetic.com/download/)

Spatial Clustering is a Wwise plug-in that merges audio objects that reach the listener from similar directions into fewer output objects. This reduces system audio object consumption in computationally demanding scenarios.

## Features

- **Clustering by direction**: inputs are grouped by their angle from the listener, so the same settings work at any world scale.
- **Persistent clusters**: an input stays in its cluster from frame to frame and only moves when it has been outside the cluster for a set time.
- **Crossfaded moves**: an input that changes cluster fades out of the old cluster and into the new one, so there are no clicks or jumps.
- **Output budget**: an optional limit on the number of outputs. When the limit is reached, the closest clusters are merged.
- **Passthrough**: inputs with more than two channels (ambiences, beds) and inputs without 3D spatialization pass through with their audio and metadata unchanged.
- **Exclusions**: high priority inputs and inputs close to the listener can be kept out of clusters.
- **Real-time safe**: memory comes from the Wwise plug-in allocator, and an update for 512 inputs takes about 30 to 60 µs. See [Performance](#performance).

## Perfect For

- Large-scale battle scenes with many weapon, impact and explosion one-shots
- Debris and particle-driven one-shots
- Crowds of sources such as footsteps or voices
- Vehicle and machinery sounds with multiple components

## Why We Built This

Most spatial audio endpoints (Dolby, Sonic, DTS) offer up to 128 concurrent spatial audio objects. In EVE Online and Frontier's massive space battles, these resources get depleted rapidly, as each spaceship generates multiple audio objects for its engines, turrets, missile launchers, and combat effects.

We developed this plugin to mix nearby sources into a single output positioned at their average direction. Human spatial resolution is limited at small angular separations, so a listener can't really tell several close sources apart from one source at the average position. The freed audio object slots then become available for sounds elsewhere in the scene.

### Results
The plugin ships as part of the EVE Online and EVE Frontier audio engine. In intense battles we measured up to 60% reduction in audio thread CPU usage.

## In-Game Impact

### Without Plugin
![Before Clustering](https://github.com/user-attachments/assets/7782ff8f-1e5e-4326-8b58-f78d59cbdbd0)

Scene with 20 ships firing at the player:
- System audio object consumption is constantly being exceeded
- Text in each unique location is obscured since quite a lot of objects are stacked upon each other
- No control over which objects are spatialized and which are redirected into the main mix.

### With Plugin
![After Clustering](https://github.com/user-attachments/assets/70c196bf-144b-405e-af78-914b2f76d4c8)

Same scene with plugin enabled:
- System audio object consumption is drastically reduced (20-24 objects)
- Clear, distinct spatial positioning for each cluster
- More resources for sound designers to add unique locations of spatialized objects

## Install

1. From [Releases](https://github.com/carbonengine/spatial-audio-clustering/releases), download `SpatialClustering_<version>_Wwise<year>.<major>.zip` for your Wwise version. The plug-in only works with the Wwise version it was built for, such as 2025.1.
2. Extract the zip.
3. In the Wwise Launcher, open **Plug-ins**, choose **Add from directory** and select the `bundle` folder from the zip. Then install Spatial Clustering into your Wwise install.

Upgrading from 2.0.0 or earlier? Uninstall the old version first, since both versions use the same plug-in IDs. The 3.0.0 release notes list the renamed files.

No release for your Wwise version? [Build it from source](#building-from-source).

## Quick Start

1. Set the channel configuration of a bus to **Audio Objects**.
2. In the bus's **Effects** tab, add the **Spatial Clustering** effect.
3. Route the sounds you want merged to that bus. Mono and stereo sounds with 3D spatialization are clustered; everything else passes through.
4. Start with the default settings, then look at the clusters in the [profiler views](#profiling) and tune the [parameters](#parameters).

## Use It in Your Game

The runtime plug-in must come from the same Wwise version as your game, since the sound engine rejects plug-ins built for another version. Installing the bundle puts the runtime files in your Wwise install's `SDK` folder.

- **Static linking**: link `SpatialClusteringFX.lib` from `SDK\x64_vc180\<Config>\lib` and include `<AK/Plugin/SpatialClusteringFXFactory.h>` in one source file.
- **Dynamic loading**: ship `SpatialClustering.dll` from `SDK\x64_vc180\<Config>\bin` next to the executable, or in the folder set in `AkInitSettings::szPluginDLLPath`.
- **CMake**: build the plug-in from source with your game, see [Build with CMake](#build-with-cmake). Link `SpatialClustering::SpatialClusteringFX` from `find_package(SpatialClustering CONFIG)` and include `<AK/Plugin/SpatialClusteringFXFactory.h>` in one source file.
- **Unity and Unreal**: add the plug-in to your project's Wwise integration the way Audiokinetic describes for plug-ins, using the same files.

## Compatibility

| Version | Wwise | Authoring | Runtime |
|---|---|---|---|
| 3.0.0 | 2025.1 | Windows x64 | Windows x64 (Visual Studio 2026) |

Other platforms are not released yet. Building for them needs the matching Wwise deployment platform; see [Building from Source](#building-from-source).

## How It Works

Spatial Clustering is an object processor. Unlike traditional Effect plug-ins, which are agnostic to Audio Objects, object processors have direct control over the Audio Objects (`AkAudioObject`) passing through a bus, so they can process their metadata and spatial characteristics.

Every audio frame the plug-in reads all audio objects on its bus, updates the clusters and writes one output object per cluster and one per passthrough input.

### Inputs

- An input can be clustered when it is mono or stereo, uses 3D spatialization (Position or Position + Orientation) and has Speaker Panning / 3D Spatialization Mix at 100.
- Cluster outputs are mono. A stereo input is mixed down with each channel at -3 dB, the Wwise stereo to mono downmix, and its spread carries over to the cluster output (see [Cluster outputs](#cluster-outputs)).
- Every other input passes through: it gets its own output with the same channel configuration, audio, positioning, custom metadata and priority. This covers inputs with more than two channels such as ambiences and beds, 2D sounds and partly spatialized sounds.
- Inputs only share a cluster when they carry the same set of custom metadata plug-in types; a cluster output carries the custom metadata of its representative input.

### Direction from the listener

On an Audio Objects bus, object positions are relative to the listener. The plug-in compares the directions of the inputs, so "nearby" means "within a given angle as heard by the listener". With **Distance Weight** above 0, a difference in distance is added to the angle on a log scale: at 1, a doubling of distance counts as much as the **Join Angle**.

Inputs at the listener position (closer than 0.0001 game units), such as sounds on the listener's own game object, have no direction. They share clusters only with each other.

### Joining and leaving

- A new input joins the nearest cluster within the **Join Angle**. If there is none, it starts a new cluster.
- When the **Max Outputs** limit is reached, a new input with no cluster within the **Join Angle** joins the nearest cluster within **Join Angle** × **Leave Ratio**. If there is none either, it still starts a new cluster, and the output budget merges the closest pair of clusters in the same frame.
- An input leaves its cluster only after it has been farther than **Join Angle** × **Leave Ratio** from the cluster for the **Dwell Time**. The gap between the two angles and the waiting time stop inputs from switching back and forth.
- A leaving input joins another cluster within the **Join Angle**, or starts its own cluster. While the **Max Outputs** limit is reached and no other cluster is within the **Join Angle**, it stays in its cluster.
- An input whose custom metadata plug-in types change leaves its cluster right away, with a crossfade, unless it is the only input in the cluster.
- A cluster with a single input follows that input and never loses it.

### Crossfaded moves

When an input moves to another cluster, its gain in the old cluster ramps down while its gain in the new cluster ramps up over the **Crossfade Time**. The two gains always add up to 1, so the input keeps its level during the move.

A switch between clustered and passthrough, for example when an RTPC moves Speaker Panning / 3D Spatialization Mix below 100, happens without a crossfade.

### Merging

Two clusters that stay within the **Join Angle** of each other for the **Dwell Time** merge. The lighter cluster fades into the other one; with **Loudness Weighting** on, the lighter cluster is the quieter one, otherwise it is the one with fewer inputs. A merge only happens if the merged cluster would still fit within the **Join Angle**.

### Output budget

With **Max Outputs** above 0, the plug-in counts clusters plus passthrough outputs. When the count is over the limit, it merges the closest pairs of clusters right away, without waiting for the dwell time. A higher priority makes a pair count as farther apart, so it merges later.

- At the limit, a new input that is farther than **Join Angle** × **Leave Ratio** from every cluster gets a cluster of its own, and the closest pair of clusters merges instead. A sound far from all others is therefore not pulled into a distant cluster.
- Passthrough inputs, excluded inputs and clusters with different custom metadata cannot be merged. If the limit cannot be reached, the plug-in posts a message to the Capture Log.
- A cluster that has been merged keeps its output until its crossfade ends, so the number of outputs can be above the limit for the length of a crossfade.

### Cluster outputs

- **Position**: the average direction of the contributing inputs, at their average distance. With **Loudness Weighting** on, each input counts by its RMS level, smoothed over about 50 ms.
- **One contributing input**: the output copies the input's positioning.
- **Several contributing inputs (including inputs fading in or out)**: the output uses Position spatialization at the cluster position with the default orientation. Its spread is the larger of the spread of the representative input (the loudest one when **Loudness Weighting** is on, otherwise the first input at full gain) and **Spread From Cluster Size** × (cluster angular radius / 180°).
- **Priority**: the highest priority among the contributing inputs.
- **Custom metadata**: copied from the representative input.
- **Name**: cluster outputs appear as `Cluster <id>` in the profiler. Passthrough outputs are named `Passthrough`, or keep the name an upstream object processor gave their input.

### Exclusions

Inputs with a priority above **Exclude Priority Above**, and inputs closer to the listener than **Near-Field Radius**, keep an output of their own and are never merged. An input that becomes excluded while it is in a cluster leaves it after the Dwell Time, with a crossfade.

### Performance

Memory comes from the Wwise plug-in allocator. It is reserved for 64 inputs in Init and grows only when more inputs arrive. An update for 512 inputs takes about 30 to 60 µs in the [benchmark](#testing-without-a-game). Lowering Max Outputs far below the current number of clusters merges them all in one update, which takes about 0.7 to 1.4 ms for 512 inputs and 1.5 to 3.5 ms for 1024 inputs in the same benchmark.

## Parameters

All parameters except Loudness Weighting can be driven by RTPCs. The same descriptions are in the help panel of the Wwise property editor.

| Parameter | Range | Default | Description |
|---|---|---|---|
| Join Angle (degrees) | 0 to 180 | 5 | Largest angle between an input and a cluster for the input to join it. 0 turns off merging by angle. |
| Leave Ratio (x Join Angle) | 1 to 4 | 1.5 | Angle at which an input starts to leave its cluster, as a multiple of the Join Angle. |
| Dwell Time (ms) | 0 to 5000 | 200 | Time an input must stay outside its cluster before it leaves, and time two clusters must stay close before they merge. |
| Crossfade Time (ms) | 0 to 1000 | 50 | Time over which a moving input fades between clusters. |
| Max Outputs (0 = no limit) | 0 to 512 | 0 | Largest number of outputs, passthrough outputs included. 0 means no limit. |
| Near-Field Radius (game units) | 0 to 1,000,000 | 0 | Inputs closer to the listener than this are never merged. The slider goes to 1000; type larger values. |
| Distance Weight | 0 to 1 | 0 | How much a difference in distance counts against the Join Angle. 0 uses the angle only. |
| Exclude Priority Above | 0 to 100 | 100 | Inputs with a priority above this value are never merged. 100 excludes nothing. |
| Spread From Cluster Size | 0 to 1 | 1 | Share of the cluster's angular size used as its minimum spread. |
| Loudness Weighting | on or off | on | Moves each cluster toward its louder inputs. |

Dwell Time and Crossfade Time are rounded to whole audio frames. At 48 kHz with 1024-sample buffers, one frame is about 21 ms. A crossfade always lasts at least one frame.

The same controls cover the whole range: a Join Angle of 180 with Max Outputs at 1 mixes all clusterable inputs into one object, as long as no passthrough or excluded inputs need outputs of their own and all inputs carry the same custom metadata. A Join Angle of 0 with a Max Outputs limit only merges when the limit is reached.

## Best Practices

### Bus Setup

- Insert the plug-in as an Effect on an Audio Objects bus (bus channel configuration set to Audio Objects). It cannot be inserted on sounds or containers and cannot be used for offline rendering.
- Use it on busses that carry many one-shots that are expensive to render as objects, such as weapon fire, impacts and debris.
- Inputs with more than two channels, such as ambience loops, always pass through untouched. If you set Max Outputs, route ambiences to another bus, since passthrough outputs count toward the limit.
- Keep important sounds, such as dialogue or the player's own weapons, out of clusters by giving them a priority above Exclude Priority Above, or by routing them to another bus.
- Use Near-Field Radius to keep sounds close to the listener separate.
- Use separate busses with their own settings for different categories, for example a smaller Join Angle for dense sources (turret fire) and a larger one for spread-out sounds (shields).

![Spatial Clustering inserted on an Audio Objects bus in Wwise](https://github.com/user-attachments/assets/88c217c1-87c9-4132-aba8-04eede2d46e1)

### Profiling

- In Audio Devices -> System monitor the system audio object consumption in real-time.
- In Views->Profiler->Audio Object List, select the bus. The Post-Effects stage shows the plug-in's outputs (`Cluster <id>` and the passthrough outputs); the Pre-Effects stage shows its inputs.
- Selections in the Audio Object List drive the Audio Object 3D Viewer, the Audio Object Meter and the Audio Object Metadata views. Use the 3D Viewer for a visual spatial representation of the clusters.
- Start a capture and check the Capture Log. The plug-in posts a message when it is not on an Audio Objects bus, when it needs more outputs than Max Outputs allows, when it cannot create output objects and when it runs out of memory. Repeated messages of the same kind are posted at most once every 5 seconds.

![Clusters in the Wwise Audio Object profiler views](https://github.com/user-attachments/assets/d46c84bb-196e-4c2a-b932-62920066b516)

## Building from Source

See Audiokinetic's [Creating Audio Plug-ins](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=effectplugin_tools_newplugin.html) for the full toolchain setup (SDK install, environment variables, compiler requirements). Wwise offers a free personal license that includes the C++ SDK and the tools needed to build this plug-in.

### Prerequisites

- Wwise 2025.1 with the SDK (C++) and the Windows deployment platform for Visual Studio 2026 (vc180), installed through the Audiokinetic Launcher. The Windows plug-in build scripts and SDK libraries come with that deployment platform.
- Visual Studio 2026 with the C++ desktop workload for the runtime plug-in, and the Windows 11 SDK 10.0.22621.0 (an individual component in the Visual Studio Installer). The Wwise 2025.1 build scripts target that SDK version for vc170 and vc180, and the build fails if only a newer SDK is installed.
- For the authoring plug-in, the vc170 toolset with MFC: Visual Studio 2022, or the MSVC v143 build tools and C++ MFC for v143 in Visual Studio 2026. The Wwise 2025.1 plug-in scripts do not accept Visual Studio 2026 for the Authoring target, and the authoring plug-in uses MFC.
- Python 3, with the `markdown` and `jinja2` packages for the property help pages (`python -m pip install markdown jinja2`).

### Build

Run these commands from the `wwise` folder in a Developer Command Prompt for VS 2026, with `WWISEROOT` set to your Wwise install:

```
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" premake Windows_vc180 -t vc180
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Windows_vc180 -c Profile -x x64 -t vc180
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Windows_vc180 -c Release -x x64 -t vc180

python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" premake Authoring -t vc170
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Authoring -c Release -x x64 -t vc170

python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Documentation
```

With only Visual Studio 2026 installed, `wp.py build Authoring` stops with "Could not find the Visual Studio Build Tools for vc170", because it looks for a Visual Studio 2022 installation to find MSBuild. The v143 toolset in Visual Studio 2026 builds the plug-in fine; run MSBuild on the solution that premake generated instead (`scripts/package.py` does this for you):

```
msbuild SpatialClustering_Authoring_Windows_vc170.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

The build copies its outputs into the Wwise install:
- Runtime plug-in: `%WWISEROOT%\SDK\x64_vc180\<Config>\lib\SpatialClusteringFX.lib` (static) and `%WWISEROOT%\SDK\x64_vc180\<Config>\bin\SpatialClustering.dll` (dynamic).
- Authoring plug-in: `%WWISEROOT%\Authoring\x64\Release\bin\Plugins\SpatialClustering.dll` and `SpatialClustering.xml`.
- Property help: `%WWISEROOT%\Authoring\Data\Plugins\SpatialClustering\Html\`, built from `wwise\WwisePlugin\res\Md`.

To also copy the runtime DLL somewhere else after each build, set `CUSTOM_WWISE_PLUGIN_DLL_PATH` for both premake and build. The DLL is then copied to `<path>\<platform>\<arch>\<toolset>\<config>\`.

### Build with CMake

The root `CMakeLists.txt` builds the runtime plug-in as a static library and installs it as the `SpatialClustering` CMake package, for games that build their Wwise plug-ins from source. It takes the Wwise SDK from a `Wwise` CMake package, such as the one a vcpkg port provides, or else from `WWISE_SDK_DIR`, which defaults to the `WWISESDK` environment variable. The Release configuration defines `AK_OPTIMIZED`, like the Release sound engine libraries.

With `-DSPATIALCLUSTERING_BUILD_AUTHORING=ON` it also builds the authoring plug-in, on Windows only. The `Authoring` install component copies it into a Wwise install with the same layout as the Launcher, which is useful for a Wwise install kept in source control: the plug-in is built against that install's exact SDK. Use the vc170 toolset (`-T v143`), because Wwise Authoring ships the Visual Studio 2022 runtime:

```
cmake -S . -B build/authoring -G "Visual Studio 18 2026" -A x64 -T v143 -DSPATIALCLUSTERING_BUILD_AUTHORING=ON -DWWISE_SDK_DIR=<Wwise SDK folder>
cmake --build build/authoring --config Release
cmake --install build/authoring --config Release --component Authoring --prefix <Wwise install>
```

The install copies `SpatialClustering.dll`, `.pdb` and `.xml` to `Authoring\x64\Release\bin\Plugins`, and the property help to `Authoring\Data\Plugins\SpatialClustering\Html`. Generate the help first with `wp.py build Documentation`.

### Package a Release

`scripts/package.py` builds everything above (Debug, Profile and Release for the runtime plug-in) and packages it as a Wwise Launcher bundle. Run it from the repository root, in the same kind of command prompt:

```
python scripts\package.py
```

It writes `dist\SpatialClustering_<version>_Wwise<year>.<major>\` with the bundle and the license files, and a zip of that folder for the GitHub release. `--skip-build` packages what is already built.

The plug-in version is set in `wwise/SpatialClusteringVersion.h`. The bundle version puts the Wwise version first and the plug-in version last, because the Launcher reads the first two numbers as the Wwise version the bundle is for: plug-in 3.0.0 built against Wwise 2025.1 becomes bundle version 2025.1.300.0.

### Testing Without a Game

The `core/tests` folder builds a standalone program around `ClusterEngine`. It does not need the Wwise SDK. It contains an assertion test suite, 13 seeded scenarios (static groups, crossing groups, one-shots, jitter, budget changes, near field, passthrough, a space battle and more) and a benchmark, which also times an update that lowers Max Outputs.

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

#### Watching a Scenario

`--trace` writes every frame of a scenario to a JSON file, which `core/tests/viewer.html` plays back in a browser. Open the viewer and drop the trace on it.

```
Release\cluster_tests scenario battle --trace battle.json
Release\cluster_tests scenario battle --seed 42 --join-angle 10 --max-outputs 12 --trace battle.json
```

The viewer shows the scene from above twice, side by side: without clustering, where every voice needs its own audio object, and with Spatial Clustering. It counts the objects in both against an endpoint limit (128 by default, editable on the page), marks the voices that would fall back to the main mix without clustering, and sums up the peak, average and saving over the whole run. `--seed` picks a different random scene, and `--join-angle` and `--max-outputs` override the scenario's settings. Adding `#t=3.2` to the viewer's address opens it paused at that time.

### Testing in a Game

Once the plug-in builds, you'll want a scene to exercise it in. A few options:
- **Unreal Engine 5 or Unity** with the Wwise integration.
- **[Wwise Integration Demo Sample](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=soundengine_integration_samplecode.html)**: a lightweight executable with various demos that you can build and modify yourself.
- **[Wwise sample games](https://www.audiokinetic.com/en/learning/samples/?searchToken=eyJwZ0Nfc2FtIjoiMSIsInBnU19zYW0iOiIxMDAiLCJzYW1fc3QiOiIifQ%3D%3D)** provided by Audiokinetic.

We don't yet ship a dedicated sample scene for spatial audio testing. That's on the list to add.

### Debugging

To place breakpoints and debug the plug-in, follow the steps [here](https://www.audiokinetic.com/qa/7840/wwise-sdk-how-step-through-code-using-visual-studio-debugger).

## Contributing

We welcome contributions. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to open issues and pull requests.

## License and Legal Notices

© 2024 - present CCP ehf.

This software was developed by CCP Games for spatial audio object clustering in EVE Online and EVE Frontier, using the Audiokinetic Wwise SDK.

This project is licensed under the [Apache License 2.0](LICENSE.txt).
