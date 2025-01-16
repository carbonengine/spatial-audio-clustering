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
  Copyright (c) 2025 CCP ehf.

This implementation was developed by CCP Games for spatial audio object clustering
in EVE Online and EVE Frontier. This implementation does not grant any rights to
CCP's trademarks or game content. EVE Online and EVE Frontier are registered
trademarks of CCP ehf.
*******************************************************************************/

#ifndef ObjectClusterFX_H
#define ObjectClusterFX_H

#include "ObjectClusterFXParams.h"
#include <AK/Plugin/PluginServices/AkMixerInputMap.h>
#include <set>
#include <string>
#include <unordered_map>
#include "KMeans.h"
#include "Utilities.h"

/**
 * @struct GeneratedObject
 * @brief Represents a generated audio object with its properties and state
 */
struct GeneratedObject {
	AK::SpeakerVolumes::MatrixPtr volumeMatrix = nullptr;
	AkAudioObjectID outputObjKey;
	int index;
	bool isClustered = false;
};

/**
 * @struct ClusterState
 * @brief Maintains the state of an audio object cluster
 */
struct ClusterState {
	AkUInt32 activeInputCount = 0;
	AkUInt16 maxFrames = 0;
};

/**
 * @class ObjectClusterFX
 * @brief Audio object clustering effect plugin
 * @details Implements audio object clustering and processing functionality as an out-of-place plugin
 */
class ObjectClusterFX : public AK::IAkOutOfPlaceObjectPlugin
{
public:
    ObjectClusterFX();
    ~ObjectClusterFX();

    /**
     * @brief Initializes the plugin
     * @param in_pAllocator Memory allocator interface
     * @param in_pContext Plugin context interface
     * @param in_pParams Plugin parameters
     * @param in_rFormat Audio format
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat) override;

    /**
     * @brief Terminates the plugin and releases resources
     * @param in_pAllocator Memory allocator interface
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT Term(AK::IAkPluginMemAlloc* in_pAllocator) override;

    /**
     * @brief Resets the plugin to its initial state
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT Reset() override;

    /**
     * @brief Retrieves plugin information
     * @param out_rPluginInfo Structure to be filled with plugin information
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT GetPluginInfo(AkPluginInfo& out_rPluginInfo) override;

    /**
     * @brief Executes the audio processing
     * @param inObjects Input audio objects and buffers
     * @param outObjects Output audio objects and buffers
     */
    void Execute(const AkAudioObjects& inObjects, const AkAudioObjects& outObjects) override;

private:
    ObjectClusterFXParams* m_pParams;
    AK::IAkPluginMemAlloc* m_pAllocator;
    AK::IAkEffectPluginContext* m_pContext;

    /**
     * @brief Forces reclustering by clearing current clusters and resetting the KMeans algorithm.
     */
    void ForceReclustering();

    /**
     * @brief Updates KMeans algorithm with input object positions
     * @param inObjects Input audio objects
     */
    void FeedPositionsToKMeans(const AkAudioObjects& inObjects);

    /**
     * @brief Prepares audio objects for processing
     * @param inObjects Input audio objects
     */
    void PrepareAudioObjects(const AkAudioObjects& inObjects);

    /**
     * @brief Processes all audio objects
     * @param inObjects Input audio objects
     */
    void ProcessAudioObjects(const AkAudioObjects& inObjects);

    /**
     * @brief Processes a clustered audio object
     * @param inObj Input audio object
     * @param inBuf Input audio buffer
     * @param outObj Output audio object
     * @param outBuf Output audio buffer
     * @param userData Generated object data
     * @param clusterState Current cluster state
     */
    void ProcessClusteredObject(
        const AkAudioObject* inObj,
        AkAudioBuffer* inBuf,
        AkAudioObject* outObj,
        AkAudioBuffer* outBuf,
        GeneratedObject* userData,
        const ClusterState& clusterState);

    /**
     * @brief Processes an unclustered audio object
     * @param inObj Input audio object
     * @param inBuf Input audio buffer
     * @param outObj Output audio object
     * @param outBuf Output audio buffer
     */
    void ProcessUnclustered(
        const AkAudioObject* inObj,
        AkAudioBuffer* inBuf,
        AkAudioObject* outObj,
        AkAudioBuffer* outBuf);

    /**
     * @brief Mixes audio into a cluster
     * @param inObject Input audio object
     * @param inBuffer Input audio buffer
     * @param outBuffer Output audio buffer
     * @param cumulativeGain Cumulative gain ramp
     * @param pGeneratedObject Generated object data
     */
    void MixToCluster(
        const AkAudioObject* inObject,
        AkAudioBuffer* inBuffer,
        AkAudioBuffer* outBuffer,
        const AkRamp& cumulativeGain,
        GeneratedObject* pGeneratedObject);

    /**
     * @brief Gets current output objects
     * @return Current output audio objects
     */
    AkAudioObjects GetCurrentOutputObjects();

    /**
     * @brief Reads cluster states from input objects
     * @param inObjects Input audio objects
     * @return Map of cluster states
     */
    std::unordered_map<AkAudioObjectID, ClusterState> ReadClusterStates(const AkAudioObjects& inObjects);

    /**
     * @brief Finds the best cluster for a position
     * @param position Position to find cluster for
     * @param existingOutputs Existing output objects
     * @param outClusterKey Output cluster identifier
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT FindBestCluster(
        const AkVector& position,
        const AkAudioObjects& existingOutputs,
        AkAudioObjectID& outClusterKey);

    /**
     * @brief Gets the cluster containing an object
     * @param objectId Object identifier
     * @return Pointer to cluster pair or nullptr if not found
     */
    const std::pair<AkVector, std::vector<AkAudioObjectID>>* GetCluster(AkAudioObjectID objectId) const;

    /**
     * @brief Allocates volume matrix memory
     * @param volumeMatrix Volume matrix pointer
     * @param in_uNumChannelsIn Number of input channels
     * @param in_uNumChannelsOut Number of output channels
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT AllocateVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix, AkUInt32 in_uNumChannelsIn, AkUInt32 in_uNumChannelsOut);

    /**
     * @brief Frees volume matrix memory
     * @param volumeMatrix Volume matrix to free
     */
    void FreeVolume(AK::SpeakerVolumes::MatrixPtr& volumeMatrix);

    /**
     * @brief Frees all volume matrices
     */
    void FreeAllVolumes();

	std::unique_ptr<KMeans> m_kmeans;
	std::unique_ptr<Utilities> m_utilities;
	std::vector<AkAudioBuffer*> m_tempBuffers;
	std::vector<AkAudioObject*> m_tempObjects;

	float m_lastDistanceThreshold = -1.0f;
    bool m_needsReclustering = false;

	/// Maps that hold KMeans clustering data
	std::vector<std::pair<AkVector, std::vector<AkAudioObjectID>>> m_clusters;

	/// Maps input objects to their corresponding output objects and processing information
	AkMixerInputMap<AkUInt64, GeneratedObject> m_mapInObjsToOutObjs;

	void UpdateClusterPositions(const AkAudioObjects& inObjects);
};

#endif // ObjectClusterFX_H