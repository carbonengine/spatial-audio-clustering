# Spatial Clustering for Wwise

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE.txt)
[![Wwise](https://img.shields.io/badge/Wwise-2025.1-green.svg)](https://www.audiokinetic.com/download/)

Spatial Clustering is a Wwise plug-in that merges audio objects that reach the listener from similar directions into fewer output objects. This reduces system audio object consumption in computationally demanding scenarios.

## Features

- **Clustering by direction**: inputs are grouped by their angle from the listener, so the same settings work at any world scale.
- **Persistent clusters**: an input stays in its cluster from frame to frame and only moves when it has been outside the cluster for a set time.
- **Crossfaded moves**: an input that changes cluster fades out of the old cluster and into the new one, so there are no clicks or jumps.
- **Output budget**: an optional limit on the number of outputs. When the limit is reached, the closest clusters are merged.
- **Passthrough**: multichannel inputs (ambiences, beds) and inputs without 3D spatialization pass through with their audio and metadata unchanged.
- **Exclusions**: high priority inputs and inputs close to the listener can be kept out of clusters.
- **Real-time safe**: memory comes from the Wwise plug-in allocator, and an update for 512 inputs takes about 30 to 60 µs. See [Performance](docs/how-it-works.md#performance).

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

## Install

1. From [Releases](https://github.com/carbonengine/spatial-audio-clustering/releases), download `SpatialClustering_<version>_Wwise<year>.<major>.zip` for your Wwise version. The plug-in only works with the Wwise version it was built for, such as 2025.1.
2. Extract the zip.
3. In the Wwise Launcher, open **Plug-ins**, choose **Add from directory** and select the `bundle` folder from the zip. Then install Spatial Clustering into your Wwise install.

Upgrading from 2.0.0 or earlier? Remove the old plug-in first, as described in the [changelog](CHANGELOG.md#upgrading-from-200-or-earlier).

No release for your Wwise version? [Build it from source](docs/building.md).

## Quick start

1. Set the channel configuration of a bus to **Audio Objects**.
2. In the bus's **Effects** tab, add the **Spatial Clustering** effect.
3. Route the sounds you want merged to that bus. Mono sounds with 3D spatialization are clustered; everything else passes through.
4. Start with the default settings, then look at the clusters in the [profiler views](docs/best-practices.md#profiling) and tune the [parameters](docs/parameters.md).

## Use it in your game

The runtime plug-in must come from the same Wwise version as your game, since the sound engine rejects plug-ins built for another version. Installing the bundle puts the runtime files in your Wwise install's `SDK` folder.

- **Static linking**: link `SpatialClusteringFX.lib` from `SDK\x64_vc180\<Config>\lib` and include `<AK/Plugin/SpatialClusteringFXFactory.h>` in one source file.
- **Dynamic loading**: ship `SpatialClustering.dll` from `SDK\x64_vc180\<Config>\bin` next to the executable, or in the folder set in `AkInitSettings::szPluginDLLPath`.
- **Unity and Unreal**: add the plug-in to your project's Wwise integration the way Audiokinetic describes for plug-ins, using the same files.

## Compatibility

| Version | Wwise | Authoring | Runtime |
|---|---|---|---|
| 3.0.0 | 2025.1 | Windows x64 | Windows x64 (Visual Studio 2026) |

Other platforms are not released yet. Building for them needs the matching Wwise deployment platform; see [Building from source](docs/building.md).

## Documentation

- [How it works](docs/how-it-works.md): inputs, clusters, merging, the output budget and performance.
- [Parameters](docs/parameters.md): ranges, defaults and what each one does.
- [Best practices](docs/best-practices.md): bus setup and profiling.
- [Building from source](docs/building.md): build, package a release, run the tests, debug.
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)

## License and Legal Notices

© 2024 - present CCP ehf.

This software was developed by CCP Games for spatial audio object clustering in EVE Online and EVE Frontier, using the Audiokinetic Wwise SDK.

This project is licensed under the [Apache License 2.0](LICENSE.txt).
