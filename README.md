# Spatial Audio Object Clustering Plugin for Wwise

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Wwise SDK](https://img.shields.io/badge/Wwise%20SDK-2022.1%2B-green.svg)](https://www.audiokinetic.com/download/)

## Overview

The Object Cluster is a Wwise plugin developed to dynamically group and manage spatial audio objects based on their spatial proximity, helping optimize and reduce system audio object consumption in complex spatial audio scenarios.

Unlike traditional Effect Plug-Ins which are agnostic to Audio Objects, Object Processors have direct control over Audio Objects (AkAudioObject) passing through a bus, enabling the processing of their metadata and spatial characteristics.

## 🛠️ Key Features

- **Dynamic Object Clustering**: Automatically groups nearby spatial audio objects in real-time using our modification of the K-means clustering algorithm
- **Resource Optimization**: Significantly reduces active audio object count while preserving spatial accuracy
- **Configurable Distance Threshold**: Fine-tune clustering radius through Wwise UI to match your game's spatial requirements
- **Adaptive Cluster Management**: Automatically adjusts cluster count based on scene complexity and handles rapid object movement

## 🎯 Perfect For

- Large-scale battle scenes with multiple sound sources
- Particle system audio (rain, debris, ambient effects)
- Crowd simulations
- Dense environmental sound design
- Vehicle/machinery sounds with multiple components

## 🔍 Why We Built This

Most spatial audio endpoints (Dolby, Sonic, DTS) offer up to 128 concurrent spatial audio objects. In EVE Online and Frontier's massive space battles, these resources get depleted rapidly, as each spaceship generates multiple audio objects - from engine thrusters and weapon turrets to missile launchers and various combat effects.

Therefore, we developed this tool with the main goal of mixing/grouping all sounds within a certain radius without sacrificing spatialization, since the objects being mixed are very close to each other.

## 🎮 In-Game Impact

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

## 🎧 Best Practices

### Bus Architecture
- Place the plugin stategicaly in busses that need clustering (e.g turrets, missiles, engines)
- Use tighter clustering for dense sources (turret fire) vs spread-out sounds (shields)

![Wwise_wZagotM3pz](https://github.com/user-attachments/assets/88c217c1-87c9-4132-aba8-04eede2d46e1)

### Profiling
- In Audio Devices -> System monitor the system audio object consuption in real-time
- From the Wwise Authoring tool use Views->Profiler->Audio Object 3D Viewer profiler  for a visual spatial representation of the clusters
- Go to Views->Profiler->Audio Object List and select different busses to change the focus of the Audio Object 3D Viewer

![Wwise_TWJ1fyWlc5](https://github.com/user-attachments/assets/d46c84bb-196e-4c2a-b932-62920066b516)

### Debugging
To place breakpoints and debug the plugin follow the steps [here](https://www.audiokinetic.com/qa/7840/wwise-sdk-how-step-through-code-using-visual-studio-debugger)


## 🔧 Technical Deep Dive

### How It Works

1. **Object Management**: 
   - Objects within a defined distance threshold are grouped into clusters
   - Each cluster is represented by a single spatial output audio object positioned at the cluster's centroid
   - Each input object's buffer is mixed into a single output buffer using Wwise's `MixNinNChannels()` API
   - Objects that are too far from any cluster or don't have spatialization remain independent

2. **Dynamic Clustering**:
   - Number of clusters is automatically determined based on number of input objects per frame
   - Clusters are created and destroyed dynamically as objects move
   - Clustering merging ensures clusters from previous frames merge with current frames if within radius
   - Smooth transitions prevent audio artifacts when objects change clusters
   - Interpolation for rapid position changes of input objects in clusters

### K-means Algorithm Implementation

Our modified K-means clustering algorithm is specifically optimized for 3D audio object clustering:

1. **Initialization Phase**:
   - Determines maximum clusters based on square root of total objects
   - Analyzes density patterns and establishes initial centroids using k-means++ selection

2. **Assignment Phase**:
   - Assigns objects to nearest centroids within threshold
   - Maintains pool of unassigned objects

3. **Update Phase**:
   - Recalculates positions and removes empty clusters
   - Forms new clusters from unassigned object groups

4. **Convergence**:
   - Iterates until clusters stabilize
   - Ensures performance with maximum iteration limit

## 🚀 Getting Started

### Prerequisites

#### Required Software
- Visual Studio 2019 or 2022 (not VSCode)
- Wwise 2022.1+ (2021 may work but is untested)
- Python 3

#### Environment Setup
- Required environment variables:
  - `WWISEROOT`
  - `WWISESDK`
- Optional environment variables:
  - `CUSTOM_WWISE_PLUGIN_DLL_PATH` for automatic DLL copying

#### Visual Studio 2019 Requirements
- Desktop development with C++ workload
- MSVC v142 - VS 2019 C++ x64/x86 build tools
- C++ ATL for latest v142 build tools (x86 & x64)
- C++ MFC for latest v142 build tools (x86 & x64)
- Windows Universal CRT SDK
- Windows 10 SDK (10.0.20348.0)
 - Different version can be specified in PremakePlugin.lua

#### Visual Studio 2022 Requirements
- Same components as VS 2019 but with v143 versions
- Note: Requires Wwise 2022.1.5 or later

#### Wwise SDK Requirements
- SDKs for your target deployment platforms (install via Wwise Launcher)

### Building from Source

1. Clone the repository:
```
git clone [repository-url]
```
2. Generate Visual Studio solutions:

```
py -3 %WWISEROOT%\Scripts\Build\Plugins\wp.py premake Authoring_Windows
```

3. Build the required solutions:
   - `ObjectCluster_Windows_vc150_static.sln` (Static library)
   - `ObjectCluster_Windows_vc150_shared.sln` (Runtime DLL)
   - `ObjectCluster_Authoring_Windows_vc150.sln` (Authoring DLL)

## 🤝 Contributing

### Opening an Issue
- Search through existing open and closed issues before creating a new one
- Include detailed information when suggesting features, especially use cases
- Bug reports should include steps to reproduce and expected behavior

### Submitting a Pull Request
- Fork the repository
- Create a feature branch
- Submit a pull request with clear description of changes
- Ensure code follows project style guidelines

We welcome all contributions, whether they're feature requests, bug fixes, documentation improvements, or new functionality.

## 📄 License

This project is licensed under the [Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0).

Copyright (c) 2024 - present CCP ehf.

This software was developed by CCP Games for spatial audio object clustering in EVE Online and EVE Frontier.
While it is released under the Apache License 2.0, please note that this does not grant any rights to CCP Games' trademarks or game content.

Built on the Audiokinetic Wwise SDK.
