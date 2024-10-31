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

#ifndef ObjectClusterFX_H
#define ObjectClusterFX_H

#include "ObjectClusterFXParams.h"
#include <AK/Plugin/PluginServices/AkMixerInputMap.h>

#include <set>
#include <string>
#include <unordered_map>

#include "KMeans.h"
#include "Utilities.h"


struct GeneratedObject
{
	AK::SpeakerVolumes::MatrixPtr volumeMatrix = nullptr;
	AkVector offset;
	AkVector currentPosition;
	AkAudioObjectID outputObjKey;
	int index;
	bool isClustered = false;
};

struct ClusterState {
	AkUInt32 activeInputCount = 0;
	AkUInt32 totalInputCount = 0;
	AkUInt16 maxFrames = 0;
	bool hasActiveInput = false;
};

class ObjectClusterFX
	: public AK::IAkOutOfPlaceObjectPlugin
{
public:
	ObjectClusterFX();
	~ObjectClusterFX();
	/// Plug-in initialization.
	/// Prepares the plug-in for data processing, allocates memory and sets up the initial conditions.
	AKRESULT Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat) override;

	/// Release the resources upon termination of the plug-in.
	AKRESULT Term(AK::IAkPluginMemAlloc* in_pAllocator) override;

	/// The reset action should perform any actions required to reinitialize the
	/// state of the plug-in to its original state (e.g. after Init() or on effect bypass).
	AKRESULT Reset() override;

	/// Plug-in information query mechanism used when the sound engine requires
	/// information about the plug-in to determine its behavior.
	AKRESULT GetPluginInfo(AkPluginInfo& out_rPluginInfo) override;

	/// Effect plug-in DSP execution.
	void Execute(
		const AkAudioObjects& inObjects,	///< Input objects and object audio buffers.
		const AkAudioObjects& outObjects	///< Output objects and object audio buffers.
		) override;


private:
	ObjectClusterFXParams* m_pParams;
	AK::IAkPluginMemAlloc* m_pAllocator;
	AK::IAkEffectPluginContext* m_pContext;

	/// Feeds the KMeans clustering algorithm with positional data from the input objects
	void FeedPositionsToKMeans(const AkAudioObjects& inObjects);

	/// Prepare audio objects state and assign them to their corresponding clusters or handle non-clustered objects.
	void PrepareAudioObjects(const AkAudioObjects& inObjects);
	/// Processes all input objects to their corresponding outputs based on their mapped destinations.
	/// Handles cluster mixing and state management for each object.
	void ProcessAudioObjects(const AkAudioObjects& inObjects);
	/// Processes a clustered audio object by mixing it into its assigned cluster and managing its state.
	void ProcessClusteredObject(
		const AkAudioObject* inObj,
		AkAudioBuffer* inBuf,
		AkAudioObject* outObj,
		AkAudioBuffer* outBuf,
		GeneratedObject* userData,
		const ClusterState& clusterState);

	/// Processes an unclustered audio object by directly copying it to its corresponding output.
	void ProcessUnclustered(
		const AkAudioObject* inObj,
		AkAudioBuffer* inBuf,
		AkAudioObject* outObj,
		AkAudioBuffer* outBuf);

	/// Mixes multiple input audio objects that belong to the same cluster into a single output object.
	void MixToCluster(
		const AkAudioObject* inObject,
		AkAudioBuffer* inBuffer,
		AkAudioBuffer* outBuffer,
		const AkRamp& cumulativeGain,
		GeneratedObject* pGeneratedObject
	);

	/// Retrieves the current set of output audio objects from the plugin context.
	AkAudioObjects GetCurrentOutputObjects();

	/// Analyzes all input objects and calculates the state of each cluster including active inputs and frame counts.
	std::unordered_map<AkAudioObjectID, ClusterState> ReadClusterStates(const AkAudioObjects& inObjects);

	/// Finds the most suitable existing cluster for a given position based on distance threshold.
	AKRESULT ObjectClusterFX::FindBestCluster(
		const AkVector& position,
		const AkAudioObjects& existingOutputs,
		AkAudioObjectID& outClusterKey);

	/// Returns the cluster containing the specified object ID, or nullptr if not found.
	const std::pair<AkVector, std::vector<AkAudioObjectID>>* GetCluster(AkAudioObjectID objectId) const;

	/// Allocates memory for the input speaker volume matrix
	AKRESULT AllocateVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix, AkUInt32 in_uNumChannelsIn, AkUInt32 in_uNumChannelsOut);

	/// Allocates memory for the input speaker volume matrix.
	void FreeVolume(AK::SpeakerVolumes::MatrixPtr& volumeMatrix);

	/// Frees memory allocated for a single volume matrix.
	void FreeAllVolumes();

	/// Member variables
	std::unique_ptr<KMeans> m_kmeans;
	std::unique_ptr<Utilities> m_utilities;
	std::vector<AkAudioBuffer*> m_tempBuffers;
	std::vector<AkAudioObject*> m_tempObjects;
	std::vector<AkAudioObjectID> m_activeClusters;

	/// Maps that hold KMeans clustering data
	std::vector<std::pair<AkVector, std::vector<AkAudioObjectID>>> m_clusters;

	/// Maps input objects to their corresponding output objects and processing information
	AkMixerInputMap<AkUInt64, GeneratedObject> m_mapInObjsToOutObjs;
};

#endif // ObjectClusterFX_H