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
#include "ClusterUtilities.h"
#include "DSPUtilities.h"
#include "SharedStructures.h"


#define AK_MAX_GENERATED_OBJECTS 128

// The plugin needs to maintain a map of input object keys to generated objects.
// Placeholder struct will be used later.

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
	ClusterUtilities m_clusterUtilities;
	DSPUtilities m_dspUtilities;

	std::map<AkTransform, std::vector<AkAudioObjectID>> m_clustersData;
	AkMixerInputMap<AkUInt64, GeneratedObjects> m_mapInObjsToOutObjs;

	// Bookeeping for all Input Objects, uses the AkMixerInputMap to keep track of them.
	void BookkeepAudioObjects(const AkAudioObjects& inObjects);
	// Utilize the map to write to the input objects to their corresponding output objects.
	void WriteToOutput(const AkAudioObjects& inObjects);
	// Mix the Input object to the output, the method will redirect the mixing
	// to either use the Wwise API or our custom mixing approach based on an RTPC.
	void MixInputToOutput(
		const AkAudioObject* inObject,
		AkAudioBuffer* inBuffer,
		AkAudioBuffer* outBuffer,
		const AkRamp& cumulativeGain,
		GeneratedObjects* pGeneratedObject
	);
	// Switch Clustering strategies based on the RTPC selection
	ClusterMap GenerateClusters(const AkAudioObjects& inObjects);

	// Helper function that tries to create unique object IDs
	AkAudioObjectID ObjectClusterFX::GenerateUniqueID()
	{
		m_uniqueClusterID += 1;
		return m_uniqueClusterID;
	}

};

#endif // ObjectClusterFX_H
