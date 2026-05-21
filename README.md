# Spatial Clustering Plugin for Wwise

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE.txt)
[![Wwise SDK](https://img.shields.io/badge/Wwise%20SDK-2025.1%2B-green.svg)](https://www.audiokinetic.com/download/)

## Overview

This wwise plugin is a performance optimisation tool developed to dynamically group and manage spatial audio objects based on their proximity, helping optimize and reduce system audio object consumption in computentionally demanding scenarios.

Unlike traditional Effect Plug-Ins which are agnostic to Audio Objects, Object Processors have direct control over Audio Objects (AkAudioObject) passing through a bus, enabling the processing of their metadata and spatial characteristics.

## Features

- **Dynamic Object Clustering**: Automatically groups nearby spatial audio objects in real-time using a density-aware spatial clustering algorithm
- **Resource Optimization**: Significantly reduces active audio object count while preserving spatial accuracy
- **Adaptive Cluster Management**: Automatically adjusts cluster count based on scene complexity and handles rapid object movement

## Perfect For

- Large-scale battle scenes with multiple sound sources
- Particle system audio (rain, debris, ambient effects)
- Crowd simulations
- Dense environmental sound design
- Vehicle/machinery sounds with multiple components

## Why We Built This

Most spatial audio endpoints (Dolby, Sonic, DTS) offer up to 128 concurrent spatial audio objects. In EVE Online and Frontier's massive space battles, these resources get depleted rapidly, as each spaceship generates multiple audio objects - from engine thrusters and weapon turrets to missile launchers and various combat effects.

Therefore, we developed this tool with the main goal of mixing/grouping all sounds within a certain radius without sacrificing spatialization, since the objects being mixed are very close to each other. 

### Results
We internally tested this with EVE Online and Frontier where this plugin acts now as a part of our engine.
In some cases we did find massive performance gains up to 60% CPU performance gains in intense battles.

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

## Best Practices

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


## Technical Deep Dive

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

### Clustering Algorithm

For the purpose of this project we developed a density-aware spatial clustering algorithm for real-time 3D audio. Given a set of audio object positions and a distance threshold, it groups nearby objects into clusters each frame and represents each cluster as a single output object at the group's centroid.

1. **Initialization**:
   Initial cluster centers are picked based on local object density using Gaussian weighting, biased toward the listener position. Additional centers are placed using K-means++ and selection stops early if all remaining objects are already within range of an existing center.

2. **Assignment**:
   Each object is assigned to its nearest cluster center if it falls within the distance threshold. Objects too far from any center are grouped with other nearby distant objects to form new clusters.

3. **Update Step**:
   Cluster centers are recalculated as the average position of their members. Empty clusters are removed. The algorithm iterates until assignments stabilize or SSE improvement becomes negligible.

## Getting Started

### Prerequisites

#### Required Software
- A C++ toolchain supported by Wwise's plugin build system
- Wwise 2025.1+ (it will work for previous version although you might need to adapt some of the code)
- Python 3

### Building from Source

1. Generate solutions:

```
py -3 %WWISEROOT%\Scripts\Build\Plugins\wp.py premake Authoring_Windows
```

2. Build the required solutions:
   - `ObjectCluster_Windows_vc170_static.sln` (Static library)
   - `ObjectCluster_Windows_vc170_shared.sln` (Runtime DLL)
   - `ObjectCluster_Authoring_Windows_vc170.sln` (Authoring DLL)

## Contributing

We welcome contributions: feature requests, bug fixes, documentation improvements, or new functionality. Contribution guidelines and PR templates are still being finalized as we open source the wider engine, so please bear with us while that work lands.

### Opening an Issue
- Search existing open and closed issues before creating a new one
- For feature requests, include the use case
- For bug reports, include steps to reproduce and expected behavior

### Submitting a Pull Request
- Fork the repository and create a feature branch
- Build and test your changes at runtime before submitting. We test anything that ships into our production pipeline anyway, but doing this on your end shows genuine interest and keeps code quality high
- Submit a PR with a clear description of what changed and why

### Building and Testing Your Changes

Wwise offers a free personal license that includes the C++ SDK and CLI tools needed to build this plugin. To get started, see [Creating Audio Plug-ins](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=effectplugin_tools_newplugin.html) in the Wwise docs.

Once the plugin builds, you'll want a scene to exercise it in. A few options:
- **Unreal Engine 5 or Unity** with the Wwise integration. Both are first-class supported by Audiokinetic.
- **[Wwise Integration Demo Sample](https://www.audiokinetic.com/en/public-library/2025.1.7_9143/?source=SDK&id=soundengine_integration_samplecode.html)**: a lightweight executable with various demos that you can build and modify yourself.
- **[Wwise sample games](https://www.audiokinetic.com/en/learning/samples/?searchToken=eyJwZ0Nfc2FtIjoiMSIsInBnU19zYW0iOiIxMDAiLCJzYW1fc3QiOiIifQ%3D%3D)** provided by Audiokinetic.

We don't yet ship a dedicated sample scene for spatial audio testing. That's on the list to add.


## License and Legal Notices

© 2024 - present CCP ehf.

This software was developed by CCP Games for spatial audio object clustering in EVE Online and EVE Frontier, using the Audiokinetic Wwise SDK.

This project is licensed under the [Apache License 2.0](LICENSE.txt).
