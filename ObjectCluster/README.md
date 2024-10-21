# 3D Audio Object Cluster Plugin
The purpose of this plugin is to reduce the number of 3D audio objects created in a scene when the user has 3D audio enabled.

3D audio objects are reduced by clustering them based on their proximity to one another. Clustered audio objects will be mixed down into a single audio object. 

## Local Development

This repository uses scripts supplied by Audiokinetic in their Wwise installations for generating Visual Studio solutions. In addition, some extras have been added to `PremakePlugin.lua` that will help make the development flow quicker.

This plugin follows the development workflow laid out by AudioKinetic in [their audio plugin documentation](https://www.audiokinetic.com/en/library/edge/?source=SDK&id=effectplugin_tools.html). If you need deeper info about what is going on then refer to their documentation. This document will only briefly cover the basics.

### Prerequisites
* A copy of Visual Studio (not VSCode)
* A Wwise SDK installation gotten through the Wwise launcher.
* A Python 3 installation.
* The existence of the Windows environment variables `WWISEROOT` and `WWISESDK`. You can set this directly in the Wwise launcher for the Wwise version you are developing against by click on `Install Options` -> `Set Environment Variables` for a given Wwise version.

### Generating Visual Studio Solutions
To generate visual studio solutions first make sure you have the environment variables `WWISEROOT` and `WWISESDK` set and pointing to a valid Wwise SDK installation. In addition to these environment variables you can also set `CUSTOM_WWISE_PLUGIN_DLL_PATH`. This will add a post build step to the shared .dll's Visual Studio solution that will copy the generated .dll to the that directory. See `PremakePlugin.lua` to see how this is configured.

Next, open up a Powershell instance and run the following command from the directory this Readme sits in:

```
py -3 $env:WWISEROOT\Scripts\Build\Plugins\wp.py premake Authoring_Windows
```
If you only had the v141 Visual Studio toolset installed locally then this would generate the following three solutions:
* ObjectCluster_Windows_vc150_static.sln – This is the static .lib used by both the Wwise authoring .dll and the Wwise SDK .dll. 

* ObjectCluster_Windows_vc150_shared.sln – This is the solution you want to use to build ObjectCluster.dll that will be used by the Wwise SDK (and therefore the game’s runtime).

* ObjectCluster_Authoring_Windows_vc150.sln – This is the solution you want to use to build ObjectCluster.dll for the Wwise authoring tool. You need this built in order to add this plugin on busses in a Wwise project.

This will also generate vc160 or any other Visual Studio toolsets you have locally installed as well. 

### Building the plugin
When working locally, you can simply open up one of the generated solutions and build them from within Visual Studio.

## Building and Packaging

To build and package this plugin for external use you can use Audio Kinetic's `wp.py` script as outlined [their documentation for building](https://www.audiokinetic.com/en/library/edge/?source=SDK&id=effectplugin_tools_building.html) and [their documentation for packaging](https://www.audiokinetic.com/en/library/edge/?source=SDK&id=effectplugin_tools_packaging.html).
