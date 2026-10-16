# Copyright 2026 Fenris Creations
#
# Licensed under the Apache License, Version 2.0. See LICENSE.txt in the repository root.

# Provides SpatialClustering::SpatialClusteringFX, the runtime plug-in as a static library.
# Link it next to the Wwise sound engine and include <AK/Plugin/SpatialClusteringFXFactory.h>
# in one source file to register the plug-in.
include(${CMAKE_CURRENT_LIST_DIR}/SpatialClusteringTargets.cmake)
