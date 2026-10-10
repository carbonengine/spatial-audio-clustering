# Spatial Clustering Plugin for Wwise

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE.txt)
[![Wwise SDK](https://img.shields.io/badge/Wwise%20SDK-2025.1%2B-green.svg)](https://www.audiokinetic.com/download/)

## Overview

The Spatial Clustering plugin (ObjectCluster) merges audio objects that reach the listener from similar directions into fewer output objects. This reduces system audio object consumption in computationally demanding scenarios.

Unlike traditional Effect Plug-Ins which are agnostic to Audio Objects, Object Processors have direct control over Audio Objects (AkAudioObject) passing through a bus, enabling the processing of their metadata and spatial characteristics.

## Features

- **Clustering by direction**: inputs are grouped by their angle from the listener, so the same settings work at any world scale.
- **Persistent clusters**: an input stays in its cluster from frame to frame and only moves when it has been outside the cluster for a set time.
- **Crossfaded moves**: an input that changes cluster fades out of the old cluster and into the new one, so there are no clicks or jumps.
- **Output budget**: an optional limit on the number of outputs. When the limit is reached, the closest clusters are merged.
- **Passthrough**: multichannel inputs (ambiences, beds) and inputs without 3D spatialization pass through with their audio and metadata unchanged.
- **Exclusions**: high priority inputs and inputs close to the listener can be kept out of clusters.
- **Real-time safe**: memory comes from the Wwise plug-in allocator. It is reserved for 64 inputs in Init and grows only when more inputs arrive. An update for 512 inputs takes about 30 to 60 µs in the [harness](#testing-without-a-game) bench. Lowering Max Outputs far below the current number of clusters merges them all in one update, which takes about 0.7 to 1.4 ms for 512 inputs and 1.5 to 3.5 ms for 1024 inputs in the same bench.

## Perfect For

- Large-scale battle scenes with many weapon, impact and explosion one-shots
- Debris and particle-driven one-shots
- Crowds of mono sources such as footsteps or voices
- Vehicle and machinery sounds with multiple mono components

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

## How It Works

Every audio frame the plug-in reads all audio objects on its bus, updates the clusters and writes one output object per cluster and one per passthrough input.

### Inputs

- An input can be clustered when it is mono, uses 3D spatialization (Position or Position + Orientation) and has Speaker Panning / 3D Spatialization Mix at 100.
- Every other input passes through: it gets its own output with the same channel configuration, audio, positioning, custom metadata and priority. This covers stereo and multichannel ambiences, beds, 2D sounds and partly spatialized sounds.
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

## Parameters

All parameters except Loudness Weighting can be driven by RTPCs.

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
- Use it on busses that carry many mono one-shots that are expensive to render as objects, such as weapon fire, impacts and debris.
- Multichannel inputs such as ambience loops always pass through untouched. If you set Max Outputs, route ambiences to another bus, since passthrough outputs count toward the limit.
- Keep important sounds, such as dialogue or the player's own weapons, out of clusters by giving them a priority above Exclude Priority Above, or by routing them to another bus.
- Use Near-Field Radius to keep sounds close to the listener separate.
- Use separate busses with their own settings for different categories, for example a smaller Join Angle for dense sources (turret fire) and a larger one for spread-out sounds (shields).

![Wwise_wZagotM3pz](https://github.com/user-attachments/assets/88c217c1-87c9-4132-aba8-04eede2d46e1)

### Profiling
- In Audio Devices -> System monitor the system audio object consumption in real-time.
- In Views->Profiler->Audio Object List, select the bus. The Post-Effects stage shows the plug-in's outputs (`Cluster <id>` and the passthrough outputs); the Pre-Effects stage shows its inputs.
- Selections in the Audio Object List drive the Audio Object 3D Viewer, the Audio Object Meter and the Audio Object Metadata views. Use the 3D Viewer for a visual spatial representation of the clusters.
- Start a capture and check the Capture Log. The plug-in posts a message when it is not on an Audio Objects bus, when it needs more outputs than Max Outputs allows, when it cannot create output objects and when it runs out of memory. Repeated messages of the same kind are posted at most once every 5 seconds.
- The runtime plug-in must be built for the same Wwise SDK version as the game, since the sound engine rejects plug-ins built for another version. A game that loads plug-in DLLs looks for ObjectCluster.dll next to the executable, or in the folder set in AkInitSettings::szPluginDLLPath. A game that links plug-ins statically links ObjectClusterFX.lib and includes ObjectClusterFXFactory.h in one source file.

![Wwise_TWJ1fyWlc5](https://github.com/user-attachments/assets/d46c84bb-196e-4c2a-b932-62920066b516)

### Debugging
To place breakpoints and debug the plugin follow the steps [here](https://www.audiokinetic.com/qa/7840/wwise-sdk-how-step-through-code-using-visual-studio-debugger)

## Implementation

| Path | Contents |
|---|---|
| `SoundEnginePlugin/ClusterEngine.h/.cpp` | The clustering. Plain C++17 with no Wwise headers; all memory goes through a `ClusterAllocator`. |
| `SoundEnginePlugin/ObjectClusterFX.h/.cpp` | The Wwise object processor: reads the input objects, runs the engine, creates and updates output objects and mixes the inputs into them with gain ramps. |
| `SoundEnginePlugin/ObjectClusterFXParams.h/.cpp` | Parameters, read from banks and RTPCs. |
| `WwisePlugin/` | The authoring plug-in and the property definitions (`ObjectCluster.xml`). |
| `harness/` | Standalone tests, scenarios and benchmark for the engine. Not part of the plug-in build. |

## Getting Started

See Audiokinetic's [Creating Audio Plug-ins](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=effectplugin_tools_newplugin.html) for the full toolchain setup (SDK install, environment variables, compiler requirements).

### Prerequisites

- Wwise 2025.1 with the SDK (C++) and the Windows deployment platform for Visual Studio 2026 (vc180), installed through the Audiokinetic Launcher. The Windows plug-in build scripts and SDK libraries come with that deployment platform.
- Visual Studio 2026 with the C++ desktop workload for the runtime plug-in.
- For the authoring plug-in, the vc170 toolset with MFC: Visual Studio 2022, or the MSVC v143 build tools and C++ MFC for v143 in Visual Studio 2026. The Wwise 2025.1 plug-in scripts do not accept Visual Studio 2026 for the Authoring target, and the authoring plug-in uses MFC.
- Python 3.

### Building from Source

Run these commands from the repository root in a Developer Command Prompt for VS 2026, with `WWISEROOT` set to your Wwise install:

```
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" premake Windows_vc180 -t vc180
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Windows_vc180 -c Profile -x x64 -t vc180
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Windows_vc180 -c Release -x x64 -t vc180

python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" premake Authoring -t vc170
python "%WWISEROOT%\Scripts\Build\Plugins\wp.py" build Authoring -c Release -x x64 -t vc170
```

The build copies its outputs into the Wwise install:
- Runtime plug-in: `%WWISEROOT%\SDK\x64_vc180\<Config>\lib\ObjectClusterFX.lib` (static) and `%WWISEROOT%\SDK\x64_vc180\<Config>\bin\ObjectCluster.dll` (dynamic).
- Authoring plug-in: `%WWISEROOT%\Authoring\x64\Release\bin\Plugins\ObjectCluster.dll` and `ObjectCluster.xml`.

To also copy the runtime DLL somewhere else after each build, set `CUSTOM_WWISE_PLUGIN_DLL_PATH` for both premake and build. The DLL is then copied to `<path>\<platform>\<arch>\<toolset>\<config>\`.

## Testing Without a Game

The `harness` folder builds a standalone program around `ClusterEngine`. It does not need the Wwise SDK. It contains an assertion test suite, 12 seeded scenarios (static groups, crossing groups, one-shots, jitter, budget changes, near field, passthrough and more) and a benchmark, which also times an update that lowers Max Outputs.

```
cmake -S harness -B harness/build
cmake --build harness/build --config Release
cd harness/build
ctest -C Release
Release\cluster_harness bench
Release\cluster_harness list
Release\cluster_harness scenario oneshots --csv oneshots.csv
```

Run `cluster_harness` with no arguments to see all modes and options. On Linux and macOS the program is in `harness/build` instead of `harness/build/Release`.

## Contributing

We welcome contributions: feature requests, bug fixes, documentation improvements, or new functionality. Contribution guidelines and PR templates are still being finalized as we open source the wider engine, so please bear with us while that work lands.

### Opening an Issue
- Search existing open and closed issues before creating a new one
- For feature requests, include the use case
- For bug reports, include steps to reproduce and expected behavior

### Submitting a Pull Request
- Fork the repository and create a feature branch
- Run the harness tests, and include bench results if you change `ClusterEngine`
- Build and test your changes at runtime before submitting.
- Submit a PR with a clear description of what changed and why

### Building and Testing Your Changes

Wwise offers a free personal license that includes the C++ SDK and CLI tools needed to build this plugin. To get started, see [Creating Audio Plug-ins](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=effectplugin_tools_newplugin.html) in the Wwise docs.

Changes to the clustering itself can be tested first with the [harness](#testing-without-a-game). Once the plugin builds, you'll want a scene to exercise it in. A few options:
- **Unreal Engine 5 or Unity** with the Wwise integration.
- **[Wwise Integration Demo Sample](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=soundengine_integration_samplecode.html)**: a lightweight executable with various demos that you can build and modify yourself.
- **[Wwise sample games](https://www.audiokinetic.com/en/learning/samples/?searchToken=eyJwZ0Nfc2FtIjoiMSIsInBnU19zYW0iOiIxMDAiLCJzYW1fc3QiOiIifQ%3D%3D)** provided by Audiokinetic.

We don't yet ship a dedicated sample scene for spatial audio testing. That's on the list to add.


## License and Legal Notices

© 2024 - present CCP ehf.

This software was developed by CCP Games for spatial audio object clustering in EVE Online and EVE Frontier, using the Audiokinetic Wwise SDK.

This project is licensed under the [Apache License 2.0](LICENSE.txt).
