/*******************************************************************************
The content of this file includes portions of the AUDIOKINETIC Wwise Technology
released in source code form as part of the SDK installer package.

Commercial License Usage

Licensees holding valid commercial licenses to the AUDIOKINETIC Wwise Technology
may use this file in accordance with the end user license agreement provided
with the software or, alternatively, in accordance with the terms contained in a
written agreement between you and Audiokinetic Inc.

Apache License Usage

Alternatively, this file may be used under the Apache License, Version 2.0 (the
"Apache License"); you may not use this file except in compliance with the
Apache License. You may obtain a copy of the Apache License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed
under the Apache License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
OR CONDITIONS OF ANY KIND, either express or implied. See the Apache License for
the specific language governing permissions and limitations under the License.

  Copyright (c) 2023 Audiokinetic Inc.
*******************************************************************************/

#ifndef SpatialClusteringFXParams_H
#define SpatialClusteringFXParams_H

#include <AK/SoundEngine/Common/IAkPlugin.h>
#include <AK/Plugin/PluginServices/AkFXParameterChangeHandler.h>

// Add parameters IDs here, those IDs should map to the AudioEnginePropertyID
// attributes in the xml property definition. They follow the order of the
// property editor, which is also the order of the parameter block.
/// ID of the join angle parameter.
static const AkPluginParamID PARAM_JOIN_ANGLE_ID = 0;
/// ID of the maximum number of outputs parameter.
static const AkPluginParamID PARAM_MAX_OUTPUTS_ID = 1;
/// ID of the parameter that sets the priority above which inputs are never merged.
static const AkPluginParamID PARAM_EXCLUDE_PRIORITY_ABOVE_ID = 2;
/// ID of the near-field radius parameter.
static const AkPluginParamID PARAM_NEAR_FIELD_RADIUS_ID = 3;
/// ID of the dwell time parameter.
static const AkPluginParamID PARAM_DWELL_TIME_ID = 4;
/// ID of the crossfade time parameter.
static const AkPluginParamID PARAM_CROSSFADE_TIME_ID = 5;
/// ID of the distance weight parameter.
static const AkPluginParamID PARAM_DISTANCE_WEIGHT_ID = 6;
/// ID of the spread from cluster size parameter.
static const AkPluginParamID PARAM_SPREAD_FROM_SIZE_ID = 7;
/// Number of parameters.
static const AkUInt32 NUM_PARAMS = 8;

/// Largest join angle in degrees.
static const AkReal32 PARAM_JOIN_ANGLE_MAX = 180.f;
/// Largest dwell time in milliseconds.
static const AkReal32 PARAM_DWELL_TIME_MAX = 5000.f;
/// Largest crossfade time in milliseconds.
static const AkReal32 PARAM_CROSSFADE_TIME_MAX = 1000.f;
/// Largest maximum number of outputs.
static const AkInt32 PARAM_MAX_OUTPUTS_MAX = 512;
/// Largest near-field radius in game units.
static const AkReal32 PARAM_NEAR_FIELD_RADIUS_MAX = 1000000.f;
/// Largest playback priority, which is also the largest Exclude Priority Above value.
static const AkInt32 PARAM_PRIORITY_MAX = 100;

/// Size in bytes of the parameter block that the authoring plug-in writes to banks.
static const AkUInt32 PARAMS_BLOCK_SIZE = 6 * sizeof(AkReal32) + 2 * sizeof(AkInt32);

/** @brief Parameters of the effect, all of which RTPCs can drive. */
struct SpatialClusteringRTPCParams
{
    AkReal32 joinAngle;           ///< Angle in degrees under which an input may join a cluster.
    AkInt32 maxOutputs;           ///< Maximum number of outputs, passthrough inputs included; 0 means no limit.
    AkInt32 excludePriorityAbove; ///< Inputs with a higher priority are never merged.
    AkReal32 nearFieldRadius;     ///< Inputs closer to the listener than this distance in game units are never merged.
    AkReal32 dwellTime;           ///< Time in milliseconds a change must last before an input leaves its cluster or two clusters merge.
    AkReal32 crossfadeTime;       ///< Time in milliseconds over which a moving input fades between clusters.
    AkReal32 distanceWeight;      ///< How much a difference in distance counts against the join angle.
    AkReal32 spreadFromSize;      ///< Share of the cluster's angular size used as its minimum spread.
};

/** @brief Parameter node of the Spatial Clustering effect. */
struct SpatialClusteringFXParams
    : public AK::IAkPluginParam
{
    /** @brief Creates a parameter node with default values. */
    SpatialClusteringFXParams();

    /** @brief Creates a copy of another parameter node. */
    SpatialClusteringFXParams(const SpatialClusteringFXParams& in_rParams);

    /** @brief Destroys the parameter node. */
    ~SpatialClusteringFXParams();

    /// Create a duplicate of the parameter node instance in its current state.
    IAkPluginParam* Clone(AK::IAkPluginMemAlloc* in_pAllocator) override;

    /// Initialize the plug-in parameter node interface.
    /// Initializes the internal parameter structure to default values or with the provided parameter block if it is valid.
    AKRESULT Init(AK::IAkPluginMemAlloc* in_pAllocator, const void* in_pParamsBlock, AkUInt32 in_ulBlockSize) override;

    /// Called by the sound engine when a parameter node is terminated.
    AKRESULT Term(AK::IAkPluginMemAlloc* in_pAllocator) override;

    /// Set all plug-in parameters at once using a parameter block.
    AKRESULT SetParamsBlock(const void* in_pParamsBlock, AkUInt32 in_ulBlockSize) override;

    /// Update a single parameter at a time and perform the necessary actions on the parameter changes.
    AKRESULT SetParam(AkPluginParamID in_paramID, const void* in_pValue, AkUInt32 in_ulParamSize) override;

    AK::AkFXParameterChangeHandler<NUM_PARAMS> m_paramChangeHandler; ///< Tracks which parameters changed since the effect last read them.

    SpatialClusteringRTPCParams RTPC; ///< Parameters that RTPCs can drive.
};

#endif // SpatialClusteringFXParams_H
