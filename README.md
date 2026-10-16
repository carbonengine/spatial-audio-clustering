# Spatial Clustering for Wwise

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE.txt)
[![Wwise](https://img.shields.io/badge/Wwise-2025.1-green.svg)](https://www.audiokinetic.com/download/)

Spatial Clustering is a Wwise plug-in that merges audio objects that reach the listener from similar directions into fewer output objects. This reduces system audio object consumption in computationally demanding scenarios.

## Features

- **Clustering by direction**: inputs are grouped by their angle from the listener, so the same settings work at any world scale.
- **Persistent clusters**: inputs stay in their cluster from frame to frame instead of being regrouped every frame.
- **Crossfaded moves**: an input that changes cluster fades from one to the other, so there are no clicks or jumps.
- **Output budget**: an optional limit on the number of outputs. Over the limit, the closest clusters merge.
- **Passthrough and exclusions**: ambiences, beds and 2D sounds pass through unchanged, and high-priority or nearby sounds can be kept out of clusters.
- **Real-time safe**: an update for 512 inputs takes about 20 to 60 µs.

## Perfect For

- Large-scale battle scenes with many weapon, impact and explosion one-shots
- Debris and particle-driven one-shots
- Crowds of sources such as footsteps or voices
- Vehicle and machinery sounds with multiple components

## Why We Built This

Most spatial audio endpoints, such as Dolby Atmos, Windows Sonic and DTS, offer up to 128 concurrent spatial audio objects. In the massive space battles of EVE Online and EVE Frontier, these run out fast, because each ship has audio objects for its engines, turrets, missile launchers and combat effects.

The plug-in mixes sources that come from nearly the same direction into one output at their average direction. A listener can't tell several close sources apart from one source at their average position, and the freed objects become available for sounds elsewhere in the scene.

The plug-in ships in the EVE Online and EVE Frontier audio engine. In intense battles we measured up to 60% less audio thread CPU usage. In an EVE Frontier test with 10 ships firing at the player, the scene used all 128 system audio objects without the plug-in, and 10 to 11 clusters with the default settings.

## In-Game Impact

### Without the Plug-in
![Before Clustering](https://github.com/user-attachments/assets/7782ff8f-1e5e-4326-8b58-f78d59cbdbd0)

Scene with 20 ships firing at the player:
- System audio object consumption is constantly exceeded.
- Objects are stacked on top of each other, so their labels overlap.
- No control over which objects are spatialized and which fall back into the main mix.

### With the Plug-in
![After Clustering](https://github.com/user-attachments/assets/70c196bf-144b-405e-af78-914b2f76d4c8)

Same scene with the plug-in enabled:
- System audio object consumption drops to 20 to 24 objects.
- Each cluster has a clear, distinct position.
- Without the plug-in, objects over the endpoint's budget fall back into the bed and lose their exact position. With it, every cluster is an audio object at its own position.
- Sound designers get objects back for other spatialized sounds.

## Install

1. From [Releases](https://github.com/carbonengine/spatial-audio-clustering/releases), download `SpatialClustering_<version>_Wwise<year>.<major>.zip` for your Wwise version. The plug-in only works with the Wwise version it was built for.
2. Extract the zip.
3. In the Wwise Launcher, open **Plug-ins**, choose **Add from directory** and select the `bundle` folder inside the extracted `SpatialClustering_<version>_Wwise<year>.<major>` folder. Then install Spatial Clustering into your Wwise install.

Upgrading from 2.0.0 or earlier? Remove the old plug-in first, see [Upgrading](docs/integration.md#upgrading-from-200-or-earlier). No release for your Wwise version? [Build it from source](docs/building.md).

## Quick Start

1. Set the channel configuration of a bus to **Audio Objects**.
2. In the bus's **Effects** tab, add the **Spatial Clustering** effect.

   ![Spatial Clustering inserted on an Audio Objects bus in Wwise](https://github.com/user-attachments/assets/88c217c1-87c9-4132-aba8-04eede2d46e1)

3. Route the sounds you want merged to that bus. Mono and stereo sounds with 3D spatialization and Speaker Panning / 3D Spatialization Mix at 100 are clustered. Everything else passes through.
4. Start with the default settings. Watch the clusters in the Audio Object 3D Viewer, then tune the [parameters](docs/parameters.md). Most scenes only need **Join Angle**, plus **Max Outputs** for a fixed object budget.

   ![Clusters in the Wwise Audio Object profiler views](https://github.com/user-attachments/assets/d46c84bb-196e-4c2a-b932-62920066b516)

## Documentation

- [How it works](docs/how-it-works.md): which inputs are clustered, how clusters form, move and merge, and performance.
- [Parameters](docs/parameters.md): ranges, defaults and what each one does.
- [Best practices](docs/best-practices.md): bus setup and profiling.
- [Use it in your game](docs/integration.md): linking the runtime plug-in and upgrading from 2.0.0.
- [Building from source](docs/building.md): toolchain, build, CMake, packaging and debugging.

## Compatibility

| Version | Wwise | Authoring | Runtime |
|---|---|---|---|
| 3.0.0 | 2025.1 | Windows x64 | Windows x64, Visual Studio 2026 |

Other platforms are not released yet. They can be built from source with the matching Wwise deployment platform.

## Contributing

Contributions are welcome. Please read the Carbon Engine [contributing guide](https://github.com/carbonengine/.github/blob/main/CONTRIBUTING.md) before opening an issue or pull request. It covers the workflow, the CLA and the pull request template, and applies to every `carbonengine` repository. Please also follow the [Code of Conduct](https://github.com/carbonengine/.github/blob/main/CODE_OF_CONDUCT.md), and report security issues privately as described in the [Security Policy](https://github.com/carbonengine/.github/blob/main/SECURITY.md) rather than in a public issue.

### Opening an Issue
- Search existing open and closed issues before creating a new one
- For feature requests, include the use case
- For bug reports, include steps to reproduce and expected behavior

### Submitting a Pull Request
- Fork the repository and create a feature branch
- Build and test your changes at runtime before submitting.
- Submit a PR with a clear description of what changed and why

### Building and Testing Your Changes

## License

© 2026 Fenris Creations

This software was developed by Fenris Creations for spatial audio object clustering in EVE Online and EVE Frontier, using the Audiokinetic Wwise SDK.

This project is licensed under the [Apache License 2.0](LICENSE.txt).
