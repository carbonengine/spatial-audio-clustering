# Use It in Your Game

The runtime plug-in must come from the same Wwise version as your game, since the sound engine rejects plug-ins built for another version. Installing the bundle puts the runtime files in your Wwise install's `SDK` folder.

- **Static linking**: link `SpatialClusteringFX.lib` from `SDK\x64_vc180\<Config>\lib` and include `<AK/Plugin/SpatialClusteringFXFactory.h>` in one source file.
- **Dynamic loading**: ship `SpatialClustering.dll` from `SDK\x64_vc180\<Config>\bin` next to the executable, or in the folder set in `AkInitSettings::szPluginDLLPath`.
- **CMake**: build the plug-in from source with your game, see [Build with CMake](building.md#build-with-cmake). Link `SpatialClustering::SpatialClusteringFX`, from `find_package(SpatialClustering CONFIG)` or from `add_subdirectory` on this repository, and include `<AK/Plugin/SpatialClusteringFXFactory.h>` in one source file.
- **Unity and Unreal**: add the plug-in to your project's Wwise integration the way Audiokinetic describes for plug-ins, using the same files.

## Upgrading from 2.0.0 or Earlier

Version 3.0.0 renames the plug-in from ObjectCluster to Spatial Clustering and replaces the clustering and its parameters.

1. Remove the old plug-in from your Wwise install, since both versions use the same company and plug-in IDs. Delete `ObjectCluster.dll` and `ObjectCluster.xml` from `Authoring\x64\Release\bin\Plugins`, and `ObjectCluster.dll` and `ObjectClusterFX.lib` from the `SDK` folders you built into.
2. Install Spatial Clustering. Wwise projects find the effect as before, but the parameters are new, so check the settings of every Spatial Clustering effect.
3. In your game, link `SpatialClusteringFX.lib` and include `SpatialClusteringFXFactory.h` instead of `ObjectClusterFX.lib` and `ObjectClusterFXFactory.h`, or ship `SpatialClustering.dll` instead of `ObjectCluster.dll`.
4. Regenerate your SoundBanks.
