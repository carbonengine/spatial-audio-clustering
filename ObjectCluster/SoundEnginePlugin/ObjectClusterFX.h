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
#include <vector>
#include <map>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <malloc.h>

#include "ObjectClusterFXParams.h"
#include <AK/Plugin/PluginServices/AkMixerInputMap.h>

#include "KMeans.h"


#define AK_MAX_GENERATED_OBJECTS 128

// The plugin needs to maintain a map of input object keys to generated objects.
// Placeholder struct will be used later.

struct GeneratedObjects
{
	bool isClustered = false;
	AkAudioObjectID uniqueClusterID = -1;
	AkAudioObjectID outputObjKey;
	int index;  /// We use an index mark each output object as "visited" and map them to input objects (index in the array) at the same time.
	AK::SpeakerVolumes::MatrixPtr volumeMatrix = nullptr;
};

struct ClusterObject
{
	std::set<AkAudioObjectID> inObjectKeys;
	AkAudioObjectID uniqueClusterID = -1;
	AkAudioObjectID outputObjKey = -1;
};

bool operator==(const AkVector& a, const AkVector& b) 
{
	return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
}

bool operator!=(const AkVector& a, const AkVector& b) 
{
	return a.X != b.X && a.Y != b.Y && a.Z != b.Z;
}

bool operator<(const AkVector& a, const AkVector& b) 
{
	if (a.X != b.X) return a.X < b.X;
	if (a.Y != b.Y) return a.Y < b.Y;
	if (a.Z != b.Z) return a.Z < b.Z;
}

bool operator == (const AkTransform a, const AkTransform b)
{
	return (a.OrientationFront() == b.OrientationFront()
		&& a.OrientationTop() == b.OrientationTop()
		&& a.Position() == b.Position()
		);
}

bool operator<(const AkTransform& a, const AkTransform& b) 
{
	if (a.OrientationFront() != b.OrientationFront()) return a.OrientationFront() < b.OrientationFront();
	if (a.OrientationTop() != b.OrientationTop()) return a.OrientationTop() < b.OrientationTop();
	if (a.Position() != b.Position()) return a.Position() < b.Position();
}

using ClusterMap = std::map<AkTransform, std::vector<AkAudioObjectID>>;

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
	int m_uniqueClusterID;

	KMeans m_kmeans;
	std::map<AkTransform, std::vector<AkAudioObjectID>> m_clustersData;
	AkMixerInputMap<AkUInt64, GeneratedObjects> m_mapInObjsToOutObjs;


	void ApplyCustomMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, const AK::SpeakerVolumes::MatrixPtr& currentVolumes);
	void ApplyWwiseMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, const AK::SpeakerVolumes::MatrixPtr& currentVolumes, GeneratedObjects* pGeneratedObject);
	void BookkeepAudioObjects(const AkAudioObjects& inObjects);
	void BookkeepPositionalClusters(const AkAudioObjects& inObjects);
	void ClearOutputBuffers(AkAudioObjects& outputObjects);
	AkAudioObjectID CreateOutputObject(const AkAudioObject* inobj, const AkAudioObjects& inObjects, const AkUInt32 index, AK::IAkEffectPluginContext* m_pContext);
	void CopyBuffer(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer);
    ClusterMap GenerateClusters(const AkAudioObjects& inObjects);
	ClusterMap GenerateKmeansClusters(const AkAudioObjects& inObjects);
	ClusterMap GeneratePositionalClusters(const AkAudioObjects& inObjects);
	void MixInputToOutput(const AkAudioObject* inObject, AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, GeneratedObjects* pGeneratedObject);
	void NormalizeBuffer(AkAudioBuffer* pBuffer);
	void WriteToOutput(const AkAudioObjects& inObjects);

	// helper function to find Objects by key
	AkAudioObject* ObjectClusterFX::FindAudioObjectByKey(const AkAudioObjects& inObjects, const AkAudioObjectID key)
	{
		for (AkUInt32 j = 0; j < inObjects.uNumObjects; j++) {
			if (inObjects.ppObjects[j]->key == key) {
				return inObjects.ppObjects[j];
			}
		}
		return nullptr;
	}

	// helper function to find Objects by key
	AkAudioBuffer* ObjectClusterFX::FindAudioObjectBufferByKey(const AkAudioObjects& inObjects, const AkAudioObjectID key)
	{
		for (AkUInt32 j = 0; j < inObjects.uNumObjects; j++) {
			if (inObjects.ppObjects[j]->key == key) {
				return inObjects.ppObjectBuffers[j];
			}
		}
		return nullptr;
	}

	// Helper function that tries to create unique object IDs
	AkAudioObjectID ObjectClusterFX::GenerateUniqueID()
	{
		m_uniqueClusterID += 1;
		return m_uniqueClusterID;
	}


	inline AKRESULT AllocateVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix, const AkUInt32 in_uNumChannelsIn, const AkUInt32 in_uNumChannelsOut)
	{
		AkUInt32 size = AK::SpeakerVolumes::Matrix::GetRequiredSize(in_uNumChannelsIn, in_uNumChannelsOut);
#if _WIN32
		volumeMatrix = (AK::SpeakerVolumes::MatrixPtr)_aligned_malloc(size, AK_SIMD_ALIGNMENT);
#else
		volumeMatrix = (AK::SpeakerVolumes::MatrixPtr)std::aligned_alloc(AK_SIMD_ALIGNMENT, size);
#endif
		return volumeMatrix ? AK_Success : AK_InsufficientMemory;
	}
};

#endif // ObjectClusterFX_H
