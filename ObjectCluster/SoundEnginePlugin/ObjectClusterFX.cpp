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

#include <set>
#include <string>
#include <unordered_map>
#include "ObjectClusterFX.h"
#include "../ObjectClusterConfig.h"
#define NOMINMAX
#include <Windows.h>
#include <AK/AkWwiseSDKVersion.h>

#include <cfloat> // For FLT_MAX
AK::IAkPlugin* CreateObjectClusterFX(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, ObjectClusterFX());
}

AK::IAkPluginParam* CreateObjectClusterFXParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, ObjectClusterFXParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(ObjectClusterFX, AkPluginTypeEffect, ObjectClusterConfig::CompanyID, ObjectClusterConfig::PluginID)


ObjectClusterFX::ObjectClusterFX()
    : m_pParams(nullptr)
    , m_pAllocator(nullptr)
    , m_pContext(nullptr)
    , m_uniqueClusterID(256)
{
}

ObjectClusterFX::~ObjectClusterFX()
{
}

AKRESULT ObjectClusterFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{
    m_pParams = (ObjectClusterFXParams*)in_pParams;
    m_mapInObjsToOutObjs.Init(in_pAllocator);
    m_pContext = in_pContext;
    m_pAllocator = in_pAllocator;

    if (in_rFormat.channelConfig.eConfigType != AK_ChannelConfigType_Objects)
        return AK_UnsupportedChannelConfig;

    return AK_Success;
}

AKRESULT ObjectClusterFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
	AK_PLUGIN_DELETE(in_pAllocator, this);
	return AK_Success;
}

AKRESULT ObjectClusterFX::Reset()
{
    return AK_Success;
}

AKRESULT ObjectClusterFX::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
	out_rPluginInfo.eType = AkPluginTypeEffect;
	out_rPluginInfo.bIsInPlace = false;
	out_rPluginInfo.bCanProcessObjects = true;
	out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;

	return AK_Success;
}

void ObjectClusterFX::Execute(
    const AkAudioObjects& inObjects,  // Input objects and object buffers.
    const AkAudioObjects& outObjects  // Output objects and object buffers.
) {
    AKASSERT(inObjects.uNumObjects > 0);

    BookkeepAudioObjects(inObjects);
    WriteToOutput(inObjects);
}

void ObjectClusterFX::ApplyCustomMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, const AK::SpeakerVolumes::MatrixPtr& currentVolumes)
{
    if (inBuffer->uValidFrames == 0 || inBuffer->NumChannels() == 0 || outBuffer->NumChannels() == 0) {
        return;
    }

    // Calculate the gain increment per sample
    AkReal32 fOneOverNumFrames = 1.f / (AkReal32)inBuffer->uValidFrames;
    AkReal32 gainIncrement = (cumulativeGain.fNext - cumulativeGain.fPrev) * fOneOverNumFrames;

    // Iterate through all frames
    for (AkUInt32 frame = 0; frame < inBuffer->uValidFrames; ++frame) {
        // Do some manual gain compensation and create a ramp
        AkReal32 currentGain = cumulativeGain.fPrev + gainIncrement * frame;
        //Iterate for every channel and mix the input to the output
        AkReal32* inSamples = inBuffer->GetChannel(0) + frame;
        for (AkUInt32 inChan = 0; inChan < inBuffer->NumChannels(); ++inChan, inSamples += inBuffer->uValidFrames) {
            //Get the current input sample
            AkReal32 inSample = *inSamples;
            AkReal32* outSamples = outBuffer->GetChannel(0) + frame;
            for (AkUInt32 outChan = 0; outChan < outBuffer->NumChannels(); ++outChan, outSamples += outBuffer->uValidFrames) {
                // The actual mixing inSample multiplied by the current gain
                *outSamples += inSample * currentGain * currentVolumes[inChan * outBuffer->NumChannels() + outChan];
            }
        }
    }

    // Make sure that the output buffer has a valid frame count
    outBuffer->uValidFrames = AkMin(outBuffer->MaxFrames(), inBuffer->uValidFrames);
	NormalizeBuffer(outBuffer);
}

void ObjectClusterFX::ApplyWwiseMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, const AK::SpeakerVolumes::MatrixPtr& currentVolumes, GeneratedObjects* pGeneratedObject)
{
	AkUInt32 uTransmixSize = AK::SpeakerVolumes::Matrix::GetRequiredSize(inBuffer->NumChannels(), outBuffer->NumChannels());

    // If mixVolumes doesn't exist, allocate and set it to the current volumes
    if (pGeneratedObject->volumeMatrix == nullptr) {
        AKRESULT eResult = AllocateVolumes(pGeneratedObject->volumeMatrix, inBuffer->NumChannels(), outBuffer->NumChannels());
        if (eResult == AK_Success) {
            AKPLATFORM::AkMemCpy(pGeneratedObject->volumeMatrix, currentVolumes, uTransmixSize);
        }
    }
    
	AK_GET_PLUGIN_SERVICE_MIXER(m_pContext->GlobalContext())->MixNinNChannels(
		inBuffer,
		outBuffer,
		cumulativeGain.fPrev,
		cumulativeGain.fNext,
        pGeneratedObject->volumeMatrix,
		currentVolumes
	);

	// Update stored volumes
	AKPLATFORM::AkMemCpy(pGeneratedObject->volumeMatrix, currentVolumes, uTransmixSize);
}

void ObjectClusterFX::BookkeepAudioObjects(const AkAudioObjects& inObjects)
{
    m_clustersData = GenerateClusters(inObjects);

    // Create a map to track which clusters already have output objects created
    std::unordered_map<int, AkAudioObjectID> clusterOutputObjects;

    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inobj = inObjects.ppObjects[i];
        AkAudioObjectID key = inobj->key;

        // Find the cluster this object belongs to
        auto clusterIt = std::find_if(m_clustersData.begin(), m_clustersData.end(),
            [&key](const auto& pair) {
                return std::find(pair.second.begin(), pair.second.end(), key) != pair.second.end();
            });

        GeneratedObjects* pEntry = m_mapInObjsToOutObjs.Exists(key);

        bool isClustered = (clusterIt != m_clustersData.end());

        if (pEntry) {
            // If the entry already exists, update the index
            pEntry->index = i;
        }
        else {

            pEntry = m_mapInObjsToOutObjs.AddInput(key);

            if (pEntry) {
                AkAudioObjectID outputObjKey;

                if (isClustered) {
                    // Get the cluster index
                    int clusterIndex = std::distance(m_clustersData.begin(), clusterIt);

                    // Check if an output object has already been created for this cluster
                    auto outputObjIt = clusterOutputObjects.find(clusterIndex);

                    if (outputObjIt == clusterOutputObjects.end()) {
                        // This is the first object in the cluster, so create a new output object
                        outputObjKey = CreateOutputObject(inobj, inObjects, i, m_pContext);

                        // Store the output object ID for this cluster
                        clusterOutputObjects[clusterIndex] = outputObjKey;
                    }
                    else {
                        // Output object already exists, reuse it
                        outputObjKey = outputObjIt->second;
                    }

				    pEntry->uniqueClusterID = GenerateUniqueID();
                    pEntry->outputObjKey = outputObjKey;
                    pEntry->isClustered = true;
                }
                else {
                    // If not clustered, create a unique output object
                    outputObjKey = CreateOutputObject(inobj, inObjects, i, m_pContext);

                    if (outputObjKey != AK_INVALID_AUDIO_OBJECT_ID) {
                        pEntry->outputObjKey = outputObjKey;
                        pEntry->isClustered = false;
                    }
                }

                // Update the index for future reference
                pEntry->index = i;
            }
        }
    }
}

void ObjectClusterFX::ClearOutputBuffers(AkAudioObjects& outputObjects) 
{
    for (AkUInt32 i = 0; i < outputObjects.uNumObjects; ++i) {
        AkAudioBuffer* pBufferOut = outputObjects.ppObjectBuffers[i];
        for (AkUInt32 j = 0; j < pBufferOut->NumChannels(); ++j) {
            // Clear the buffer to zero
            std::fill(pBufferOut->GetChannel(j), pBufferOut->GetChannel(j) + pBufferOut->MaxFrames(), 0.0f);
        }
    }
}

AkAudioObjectID ObjectClusterFX::CreateOutputObject(const AkAudioObject* inobj, const AkAudioObjects& inObjects, const AkUInt32 index, AK::IAkEffectPluginContext* m_pContext)
{
    AkAudioObjectID outputObjKey = AK_INVALID_AUDIO_OBJECT_ID;

    AkUInt32 numObjsOut = 1;

    // Allocate space for a new output object
    AkAudioObject* newObject = (AkAudioObject*)AkAlloca(sizeof(AkAudioObject*));
    AkAudioObjects outputObjects;
    outputObjects.uNumObjects = numObjsOut;
    outputObjects.ppObjectBuffers = nullptr;
    outputObjects.ppObjects = &newObject;

    if (m_pContext->CreateOutputObjects(inObjects.ppObjectBuffers[index]->GetChannelConfig(), outputObjects) == AK_Success)
    {
        AkAudioObject* pObject = newObject;
        outputObjKey = pObject->key;

        pObject->CopyContents(*inobj);
        pObject->positioning.behavioral.spatMode = inobj->positioning.behavioral.spatMode;
        pObject->positioning.threeD = inobj->positioning.threeD;
    }

    return outputObjKey;
}

void ObjectClusterFX::CopyBuffer(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer)
{
	for (AkUInt32 j = 0; j < inBuffer->NumChannels(); ++j)
	{
		AkReal32* pInBuf = inBuffer->GetChannel(j);
		AkReal32* outBuf = outBuffer->GetChannel(j);
		memcpy(outBuf, pInBuf, inBuffer->uValidFrames * sizeof(AkReal32));
	}
}

 ClusterMap ObjectClusterFX::GenerateClusters(const AkAudioObjects& inObjects)
{
    return m_pParams->RTPC.useKmeansClustering
        ? GenerateKmeansClusters(inObjects)
        : GeneratePositionalClusters(inObjects);
}

ClusterMap ObjectClusterFX::GenerateKmeansClusters(const AkAudioObjects& inObjects)
{
    // Iterate through input objects and get ID and position
    std::vector<ObjectPosition> objectPositions;
    objectPositions.reserve(inObjects.uNumObjects);

    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inobj = inObjects.ppObjects[i];
        objectPositions.push_back({ inobj->positioning.threeD.xform.Position(), inobj->key });
    }

    // Set the distance param before clustering
    m_kmeans.setDistanceThreshold(m_pParams->RTPC.distanceThreshold);
    m_kmeans.setTolerance(m_pParams->RTPC.tolerance);

    m_kmeans.performClustering(objectPositions);
    return m_kmeans.getClusters();
}

 ClusterMap ObjectClusterFX::GeneratePositionalClusters(const AkAudioObjects& inObjects)
{

	std::map<AkTransform, std::vector<AkAudioObjectID>> clusterMap;
    // Create new position clusters if needed and update existing ones.
    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i)
    {
        AkAudioObject* inObject = inObjects.ppObjects[i];

        auto it = clusterMap.find(inObject->positioning.threeD.xform);
        if (it == clusterMap.end())
        {
            std::vector<AkAudioObjectID> inObjectKeys;
            inObjectKeys.push_back(inObject->key);
            clusterMap.insert(std::make_pair(inObject->positioning.threeD.xform, inObjectKeys));
        }
        else
        {
            it->second.push_back(inObject->key);
        }
    }

    return clusterMap;
}

void ObjectClusterFX::MixInputToOutput(const AkAudioObject* inObject, AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, GeneratedObjects* pGeneratedObject)
{
    if (inBuffer->uValidFrames == 0 || inBuffer->NumChannels() == 0 || outBuffer->NumChannels() == 0) {
        return;
    }

	AkUInt32 uTransmixSize = AK::SpeakerVolumes::Matrix::GetRequiredSize(inBuffer->NumChannels(), outBuffer->NumChannels());
	AK::SpeakerVolumes::MatrixPtr currentVolumes = (AK::SpeakerVolumes::MatrixPtr)AkAllocaSIMD(uTransmixSize);
	//AK::SpeakerVolumes::Matrix::Zero(mx, inBuffer->NumChannels(), outBuffer->NumChannels());

	AKRESULT result = m_pContext->GetMixerCtx()->ComputePositioning(
		inObject->positioning,
		inBuffer->GetChannelConfig(),
		outBuffer->GetChannelConfig(),
	    currentVolumes	
	);

    if (m_pParams->RTPC.useCustomDSP)
    {
        ApplyCustomMix(inBuffer, outBuffer, cumulativeGain, currentVolumes);
    }
    else
    {
        ApplyWwiseMix(inBuffer, outBuffer, cumulativeGain, currentVolumes, pGeneratedObject);

    }
}

void ObjectClusterFX::NormalizeBuffer(AkAudioBuffer* pBuffer)
{
    if (pBuffer->uValidFrames == 0 || pBuffer->NumChannels() == 0) {
        return;
    }

    // Find the maximum absolute sample value across all channels
    AkReal32 maxSample = 0.0f;
    AkUInt32 numChannels = pBuffer->NumChannels();
    AkUInt32 validFrames = pBuffer->uValidFrames;

    for (AkUInt32 chan = 0; chan < numChannels; ++chan) {
        AkReal32* pChannel = pBuffer->GetChannel(chan);
        for (AkUInt32 frame = 0; frame < validFrames; ++frame) {
            maxSample = std::max(maxSample, std::abs(pChannel[frame]));
        }
    }

    // If the maximum sample is greater than 1.0, normalize the buffer
    if (maxSample > 1.0f) {
        AkReal32 normalizationFactor = 1.0f / maxSample;
        for (AkUInt32 chan = 0; chan < numChannels; ++chan) {
            AkReal32* pChannel = pBuffer->GetChannel(chan);
            for (AkUInt32 frame = 0; frame < validFrames; ++frame) {
                pChannel[frame] *= normalizationFactor;
            }
        }
    }
}

void ObjectClusterFX::WriteToOutput(const AkAudioObjects& inObjects)
{
    std::set<AkAudioObjectID> visitedClusters;
  
    AkAudioObjects outputObjects;
    outputObjects.uNumObjects = 0;
    outputObjects.ppObjectBuffers = nullptr;
    outputObjects.ppObjects = nullptr;
    m_pContext->GetOutputObjects(outputObjects);

    if (outputObjects.uNumObjects > 0)
    {
        AkAudioBuffer** buffersOut = static_cast<AkAudioBuffer**>(AkAlloca(outputObjects.uNumObjects * sizeof(AkAudioBuffer*)));
        AkAudioObject** objectsOut = static_cast<AkAudioObject**>(AkAlloca(outputObjects.uNumObjects * sizeof(AkAudioObject*)));
        outputObjects.ppObjectBuffers = buffersOut;
        outputObjects.ppObjects = objectsOut;
        m_pContext->GetOutputObjects(outputObjects);

        ClearOutputBuffers(outputObjects);

        // Iterate through our internal map.
        AkMixerInputMap<AkAudioObjectID, GeneratedObjects>::Iterator it = m_mapInObjsToOutObjs.Begin();
        while (it != m_mapInObjsToOutObjs.End())
        {
            // Has the input object been passed to Execute?
            if ((*it).pUserData->index >= 0)
            {
                // Retrieve the input buffer.
                AkAudioBuffer* inbuf = inObjects.ppObjectBuffers[(*it).pUserData->index];
                AkAudioObject* inObj = inObjects.ppObjects[(*it).pUserData->index];
                const AkUInt32 uNumChannels = inbuf->NumChannels();

                // Find the correct output object.
                AkAudioBuffer* pBufferOut = nullptr;
                AkAudioObject* pObjOut = nullptr;

                for (AkUInt32 i = 0; i < outputObjects.uNumObjects; i++)
                {
                    if (objectsOut[i]->key == (*it).pUserData->outputObjKey)
                    {
                        pBufferOut = buffersOut[i];
                        pObjOut = objectsOut[i];
                        break;
                    }
                }

                if (pObjOut)
                {
                    if ((*it).pUserData->isClustered != true)
                    {
                        // If not clustered, simply copy the input to the output.
                        CopyBuffer(inbuf, pBufferOut);

                        // Also, propagate the associated input object's custom metadata to the output.
                        std::string objName = "Not clustered";
                        pObjOut->SetName(m_pAllocator, objName.c_str());
                        pBufferOut->uValidFrames = inbuf->uValidFrames;
                    }
                    else
                    {
                        MixInputToOutput(inObj, inbuf, pBufferOut, inObj->cumulativeGain, (*it).pUserData);

                        // Copy state.
                        std::string objName = std::to_string((*it).pUserData->uniqueClusterID);
                        pObjOut->SetName(m_pAllocator, objName.c_str());
                        pBufferOut->uValidFrames = inbuf->MaxFrames();
                    }


                    // Set output buffer state to indicate that it contains data.
                    pBufferOut->eState = inbuf->eState != AK_NoMoreData ? inbuf->eState : AK_NoMoreData;
                    // Also, propagate the associated input object's custom metadata to the output.
                    pObjOut->arCustomMetadata.Copy(inObj->arCustomMetadata);
                }
                (*it).pUserData->index = -1;    // "clear" index for next frame.
                ++it;
            }
            else
            {
                // Destroy stale objects.
                it = m_mapInObjsToOutObjs.EraseSwap(it);
            }
        }
    }
}
