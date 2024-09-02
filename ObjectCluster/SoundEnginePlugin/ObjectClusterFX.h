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
#include <unordered_map>

#include <cstdlib> 
#include <cstring>
#include <malloc.h>

#include "ObjectClusterFXParams.h"
#include <AK/Plugin/PluginServices/AkMixerInputMap.h>

#include "KMeans.h"

struct GeneratedObjects

{
	bool isClustered = false;

	AkAudioObjectID outputObjKey;

	AK::SpeakerVolumes::MatrixPtr volumeMatrix = nullptr;

	int index;  /// We use an index mark each output object as "visited" and map them to input objects (index in the array) at the same time.

};

bool operator<(const AkVector& a, const AkVector& b)
{
	if (a.X != b.X) return a.X < b.X;
	if (a.Y != b.Y) return a.Y < b.Y;
	if (a.Z != b.Z) return a.Z < b.Z;
}


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
		const AkAudioObjects& in_objects,	///< Input objects and object audio buffers.
		const AkAudioObjects& out_objects	///< Output objects and object audio buffers.
	) override;

private:
	ObjectClusterFXParams* m_pParams;
	AK::IAkPluginMemAlloc* m_pAllocator;
	AK::IAkEffectPluginContext* m_pContext;
	AkChannelConfig m_channelConfig;

	/// Create the output objects.
	AkAudioObjectID CreateOutputObject(AkAudioObject* inobj, const AkAudioObjects& in_objects, AkUInt32 index, AK::IAkEffectPluginContext* m_pContext);

	/// Clear the output buffers of the output objects.
	void ObjectClusterFX::ClearOutputBuffers(AkAudioObjects& outputObjects);

	/// Mix the input audio buffer to the output audio buffer.
	void ObjectClusterFX::MixInputToOutput(
		AkAudioObject* inobj,
		AkAudioBuffer* inbuf,
		AkAudioBuffer* pBufferOut,
		const AkRamp& cumulativeGain,
		AK::SpeakerVolumes::MatrixPtr mxCurrent,
		AK::SpeakerVolumes::MatrixPtr mxPrevious
	);

	void ObjectClusterFX::NormalizeBuffer(AkAudioBuffer* pBuffer);

	void ObjectClusterFX::ApplyCustomMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, AK::SpeakerVolumes::MatrixPtr& currentVolumes);
	
	inline AKRESULT AllocateVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix, AkUInt32 in_uNumChannelsIn, AkUInt32 in_uNumChannelsOut)
	{
		AkUInt32 size = AK::SpeakerVolumes::Matrix::GetRequiredSize(in_uNumChannelsIn, in_uNumChannelsOut);

#if _WIN32
		volumeMatrix = (AK::SpeakerVolumes::MatrixPtr)_aligned_malloc(size, AK_SIMD_ALIGNMENT);
#else
		volumeMatrix = (AK::SpeakerVolumes::MatrixPtr)std::aligned_alloc(AK_SIMD_ALIGNMENT, size);
#endif
		return volumeMatrix ? AK_Success : AK_InsufficientMemory;
	}

	AkMixerInputMap<AkUInt64, GeneratedObjects> m_mapInObjsToOutObjs;
	std::map<AkVector, std::vector<AkAudioObjectID>> clustersData;

	KMeans kmeans;
};

#endif // ObjectClusterFX_H
