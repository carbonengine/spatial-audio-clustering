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

#include "ObjectClusterFX.h"
#include "../ObjectClusterConfig.h"
#define NOMINMAX
#include <Windows.h>
#include <AK/AkWwiseSDKVersion.h>
#include <string>


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
	out_rPluginInfo.bUsesGainAttribute = true;
    out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;

    return AK_Success;
}

void ObjectClusterFX::Execute(
    const AkAudioObjects& in_objects,  // Input objects and object buffers.
    const AkAudioObjects& out_objects  // Output objects and object buffers.
) {
    AKASSERT(in_objects.uNumObjects > 0);


    // Iterate through input objects and get ID and position
    std::vector<ObjectPosition> objectPositions;
    objectPositions.reserve(in_objects.uNumObjects);

    for (AkUInt32 i = 0; i < in_objects.uNumObjects; ++i) {
        AkAudioObject* inobj = in_objects.ppObjects[i];
        objectPositions.push_back({ inobj->positioning.threeD.xform.Position(), inobj->key });
    }

    // Set the distance param before clustering
    kmeans.setDistanceThreshold(m_pParams->RTPC.distanceThreshold);
    kmeans.setTolerance(m_pParams->RTPC.tolerance);

    kmeans.performClustering(objectPositions);
    clustersData = kmeans.getClusters();

    // Create a map to track which clusters already have output objects created
    std::unordered_map<int, AkAudioObjectID> clusterOutputObjects;

    for (AkUInt32 i = 0; i < in_objects.uNumObjects; ++i) {
        AkAudioObject* inobj = in_objects.ppObjects[i];
        AkAudioObjectID key = inobj->key;

        // Find the cluster this object belongs to
        auto clusterIt = std::find_if(clustersData.begin(), clustersData.end(),
            [&key](const auto& pair) {
                return std::find(pair.second.begin(), pair.second.end(), key) != pair.second.end();
            });

        GeneratedObjects* pEntry = m_mapInObjsToOutObjs.Exists(key);

        bool isClustered = (clusterIt != clustersData.end());

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
                    int clusterIndex = std::distance(clustersData.begin(), clusterIt);

                    // Check if an output object has already been created for this cluster
                    auto outputObjIt = clusterOutputObjects.find(clusterIndex);

                    if (outputObjIt == clusterOutputObjects.end()) {
                        // This is the first object in the cluster, so create a new output object
                        outputObjKey = CreateOutputObject(inobj, in_objects, i, m_pContext);

                        // Store the output object ID for this cluster
                        clusterOutputObjects[clusterIndex] = outputObjKey;
                    }
                    else {
                        // Output object already exists, reuse it
                        outputObjKey = outputObjIt->second;
                    }

                    pEntry->outputObjKey = outputObjKey;
                    pEntry->isClustered = true;
                }
                else {
                    // If not clustered, create a unique output object
                    outputObjKey = CreateOutputObject(inobj, in_objects, i, m_pContext);

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

    // First, query the number of output objects.
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

        // Clear the output buffers.
		ClearOutputBuffers(outputObjects);

        // Iterate through our internal map.
        AkMixerInputMap<AkAudioObjectID, GeneratedObjects>::Iterator it = m_mapInObjsToOutObjs.Begin();

        while (it != m_mapInObjsToOutObjs.End())
        {
            // Has the input object been passed to Execute?
            if ((*it).pUserData->index >= 0)
            {
                // Retrieve the input buffer.
                AkAudioBuffer* inbuf = in_objects.ppObjectBuffers[(*it).pUserData->index];
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
                    if ((*it).pUserData->isClustered == true)
                    {
						MixInputToOutput(in_objects.ppObjects[(*it).pUserData->index], inbuf, pBufferOut, in_objects.ppObjects[(*it).pUserData->index]->cumulativeGain, nullptr, (*it).pUserData->volumeMatrix);

                        std::string objName = std::to_string((*it).pUserData->outputObjKey);
                        pObjOut->SetName(m_pAllocator, objName.c_str());
                    }
                    else
                    {
                        // If not clustered, simply copy the input to the output.
                        for (AkUInt32 j = 0; j < uNumChannels; ++j)
                        {
                            AkReal32* pInBuf = inbuf->GetChannel(j);
                            AkReal32* outBuf = pBufferOut->GetChannel(j);
                            memcpy(outBuf, pInBuf, inbuf->uValidFrames * sizeof(AkReal32));
                        }

                        std::string objName = "Not clustered";
                        pObjOut->SetName(m_pAllocator, objName.c_str());
                    }

                    // Set output buffer state to indicate that it contains data.
                    pBufferOut->uValidFrames = inbuf->uValidFrames;
                    pBufferOut->eState = inbuf->eState;

                    // Also, propagate the associated input object's custom metadata to the output.
                    pObjOut->arCustomMetadata.Copy(in_objects.ppObjects[(*it).pUserData->index]->arCustomMetadata);
                }

                // Clear the index for the next frame.
                (*it).pUserData->index = -1;
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

AkAudioObjectID ObjectClusterFX::CreateOutputObject(AkAudioObject* inobj, const AkAudioObjects& in_objects, AkUInt32 index, AK::IAkEffectPluginContext* m_pContext)
{
    AkAudioObjectID outputObjKey = AK_INVALID_AUDIO_OBJECT_ID;

    AkUInt32 numObjsOut = 1;

    // Allocate space for a new output object
    AkAudioObject* newObject = (AkAudioObject*)AkAlloca(sizeof(AkAudioObject*));
    AkAudioObjects outputObjects;
    outputObjects.uNumObjects = numObjsOut;
    outputObjects.ppObjectBuffers = nullptr;
    outputObjects.ppObjects = &newObject;

    if (m_pContext->CreateOutputObjects(in_objects.ppObjectBuffers[index]->GetChannelConfig(), outputObjects) == AK_Success)
    {
        AkAudioObject* pObject = newObject;
        outputObjKey = pObject->key;

        //pObject->CopyContents(*inobj);
        pObject->positioning.behavioral.spatMode = inobj->positioning.behavioral.spatMode;
        pObject->positioning.threeD = inobj->positioning.threeD;
    }

    return outputObjKey;
}

void ObjectClusterFX::MixInputToOutput(
    AkAudioObject* inobj,
    AkAudioBuffer* inbuf,
    AkAudioBuffer* pBufferOut,
    const AkRamp& cumulativeGain,
    AK::SpeakerVolumes::MatrixPtr mxCurrent,
	AK::SpeakerVolumes::MatrixPtr mxPrevious
) {
    if (inbuf->uValidFrames == 0 || inbuf->NumChannels() == 0 || pBufferOut->NumChannels() == 0) {
        return;
    }

    // Prepare the mixing matrix for this input
    AkUInt32 uTransmixSize = AK::SpeakerVolumes::Matrix::GetRequiredSize(
        inbuf->NumChannels(),
        pBufferOut->NumChannels()
    );
    
    mxCurrent = (AK::SpeakerVolumes::MatrixPtr)AkAllocaSIMD(uTransmixSize);

    // Zero out the mixing matrix
    AK::SpeakerVolumes::Matrix::Zero(mxCurrent, inbuf->NumChannels(), pBufferOut->NumChannels());

    // Compute panning gains and fill the mixing matrix.

    m_pContext->GetMixerCtx()->ComputePositioning(
        inobj->positioning,
        inbuf->GetChannelConfig(),
        pBufferOut->GetChannelConfig(),
        mxCurrent
    );

    if (mxPrevious == nullptr) {
		AKRESULT eResult = AllocateVolumes(mxPrevious, inbuf->NumChannels(), pBufferOut->NumChannels());
        if (eResult == AK_Success) {
			AKPLATFORM::AkMemCpy(mxPrevious, mxCurrent, uTransmixSize);
        }
    }

    // Mix the channels of the input object into the clustered output object
    AK_GET_PLUGIN_SERVICE_MIXER(m_pContext->GlobalContext())->MixNinNChannels(
        inbuf,
        pBufferOut,
        cumulativeGain.fPrev, // Gain for the input object
        cumulativeGain.fNext, // Gain for the output object
        mxPrevious,
        mxCurrent
    );

    AKPLATFORM::AkMemCpy(mxPrevious, mxCurrent, uTransmixSize);
}

void ObjectClusterFX::ClearOutputBuffers(AkAudioObjects& outputObjects) {
    for (AkUInt32 i = 0; i < outputObjects.uNumObjects; ++i) {
        AkAudioBuffer* pBufferOut = outputObjects.ppObjectBuffers[i];
        for (AkUInt32 j = 0; j < pBufferOut->NumChannels(); ++j) {
            // Clear the buffer to zero
            std::fill(pBufferOut->GetChannel(j), pBufferOut->GetChannel(j) + pBufferOut->MaxFrames(), 0.0f);
        }
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

void ObjectClusterFX::ApplyCustomMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, AK::SpeakerVolumes::MatrixPtr& currentVolumes)
{
    // Calculate the gain increment per sample
    AkReal32 fOneOverNumFrames = 1.f / (AkReal32)inBuffer->uValidFrames;
    AkReal32 gainIncrement = (cumulativeGain.fNext - cumulativeGain.fPrev) * fOneOverNumFrames;

    // Iterate through all frames
    for (AkUInt32 frame = 0; frame < inBuffer->uValidFrames; ++frame) {
        // Do some manual gain compensation and create a ramp
        AkReal32 currentGain = cumulativeGain.fPrev + gainIncrement * frame;
        // Iterate for every channel and mix the input to the output
        AkReal32* inSamples = inBuffer->GetChannel(0) + frame;
        for (AkUInt32 inChan = 0; inChan < inBuffer->NumChannels(); ++inChan, inSamples += inBuffer->uValidFrames) {
            // Get the current input sample
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




