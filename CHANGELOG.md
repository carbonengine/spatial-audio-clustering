# Changelog

## 3.0.0 (unreleased)

Built for Wwise 2025.1.

### Changed
- The plug-in is now called **Spatial Clustering** (`SpatialClustering`); it was ObjectCluster. The DLL, static library, factory header and Launcher bundle use the new name.
- New clustering engine. Inputs are grouped by their direction from the listener into clusters that persist from frame to frame. Moves between clusters are crossfaded, and an optional output budget merges the closest clusters. Multichannel and 2D inputs pass through unchanged, and high-priority and near-field inputs can be kept out of clusters. See [How it works](docs/how-it-works.md).
- The Distance Threshold parameter is replaced by Join Angle, Leave Ratio, Dwell Time, Crossfade Time, Max Outputs, Near-Field Radius, Distance Weight, Exclude Priority Above, Spread From Cluster Size and Loudness Weighting. See [Parameters](docs/parameters.md).

### Added
- Release bundle for the Wwise Launcher.
- Help for every parameter in the Wwise property editor.
- Version information in the authoring plug-in DLL.
- A standalone program in `core/tests` with tests, scenarios and a benchmark for the clustering engine, and a browser viewer that plays back a scenario frame by frame.

### Upgrading from 2.0.0 or earlier
- Remove the old plug-in from your Wwise install before installing this one, since both use the same plug-in IDs. Uninstall it in the Wwise Launcher if you installed it there. Otherwise delete `ObjectCluster.dll` and `ObjectCluster.xml` from `Authoring\x64\Release\bin\Plugins`, and `ObjectCluster.dll` and `ObjectClusterFX.lib` from the `SDK` folders you built into.
- The plug-in keeps its company and plug-in IDs (64 and 6973), so Wwise projects refer to it the same way. The parameters are new, so check the settings of every Spatial Clustering effect after upgrading.
- In your game, link `SpatialClusteringFX.lib` and include `SpatialClusteringFXFactory.h` instead of `ObjectClusterFX.lib` and `ObjectClusterFXFactory.h`, or ship `SpatialClustering.dll` instead of `ObjectCluster.dll`. Then regenerate your SoundBanks.

## 2.0.0 (2026-05-21)

- Upgrade to the Wwise 2025.1 SDK, with minor fixes.
- Update the Apache License 2.0 format.

## 1.0.1 (2025-07-11)

- Fix a crash with the Wwise 2023 SDK by removing unnecessary copying of metadata while processing objects that are not clustered.

## 1.0.0 (2025-03-06)

- First release of the plug-in, as ObjectCluster.
