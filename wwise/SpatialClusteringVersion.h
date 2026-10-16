/*
 * Copyright 2026 CCP ehf.
 *
 * This software was developed by CCP Games for spatial audio object clustering
 * in EVE Online and EVE Frontier.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This license does not grant any rights to CCP's trademarks or game content.
 * EVE Online and EVE Frontier are registered trademarks of CCP ehf.
 */

#ifndef SpatialClusteringVersion_H
#define SpatialClusteringVersion_H

// The plug-in version, kept only here. The authoring plug-in's resource script uses it for the
// DLL version, and scripts/package.py reads it to version the Wwise Launcher bundle.
// Keep this file to plain #defines, since the resource compiler includes it.
#define SPATIALCLUSTERING_VERSION_MAJOR 3
#define SPATIALCLUSTERING_VERSION_MINOR 0
#define SPATIALCLUSTERING_VERSION_PATCH 0
#define SPATIALCLUSTERING_VERSION_STRING "3.0.0"

#endif // SpatialClusteringVersion_H
