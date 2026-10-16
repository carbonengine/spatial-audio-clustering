# Contributing

We welcome contributions: feature requests, bug fixes, documentation improvements, or new functionality.

## Opening an issue

- Search existing open and closed issues before creating a new one.
- For bug reports, use the bug report template. Include the plug-in and Wwise versions, the platform, steps to reproduce and any Capture Log messages from the plug-in.
- For feature requests, include the use case.

## Submitting a pull request

- Fork the repository and create a feature branch.
- Build and test your changes at runtime before submitting.
- Submit a PR with a clear description of what changed and why.

Contributions are accepted under the [Apache License 2.0](LICENSE.txt), the license of this project.

## Building and testing your changes

[Building from Source](README.md#building-from-source) in the README covers the toolchain, the plug-in build and ways to try the plug-in in a game.

## Where things are

| Path | Contents |
|---|---|
| `core/src/ClusterEngine.h/.cpp` | The clustering. Plain C++17 with no Wwise headers; all memory goes through a `ClusterAllocator`. |
| `wwise/` | The Wwise plug-in project. Run `wp.py` from this folder. |
| `wwise/SoundEnginePlugin/SpatialClusteringFX.h/.cpp` | The Wwise object processor: reads the input objects, runs the engine, creates and updates output objects and mixes the inputs into them with gain ramps. |
| `wwise/SoundEnginePlugin/SpatialClusteringFXParams.h/.cpp` | Parameters, read from banks and RTPCs. |
| `wwise/WwisePlugin/` | The authoring plug-in, the property definitions (`SpatialClustering.xml`) and the property help (`res/Md`). |
| `wwise/SpatialClusteringVersion.h` | The plug-in version. |
| `wwise/SpatialClusteringConfig.h` | The company and plug-in IDs. |
| `scripts/package.py` | Builds and packages a release. |

When you add or change a parameter, update `SpatialClustering.xml`, `GetBankParameters` in `SpatialClusteringPlugin.cpp`, `SpatialClusteringFXParams`, the help page in `wwise/WwisePlugin/res/Md/en` and the Parameters table in `README.md`. The order in which `GetBankParameters` writes the values must match the order in which `SpatialClusteringFXParams` reads them.
