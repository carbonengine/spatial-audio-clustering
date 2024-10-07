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
#include <AK/AkWwiseSDKVersion.h>

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

void ObjectClusterFX::BookkeepAudioObjects(const AkAudioObjects& inObjects)
{
    PopulateClusters(inObjects);

    // Create a map to track which clusters already have output objects created
    std::unordered_map<int, AkAudioObjectID> clusterOutputObjects;

    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inobj = inObjects.ppObjects[i];
        AkAudioObjectID key = inobj->key;

        // Find the cluster this object belongs to
        auto clusterIt = std::find_if(m_clusterMap.begin(), m_clusterMap.end(),
            [&key](const auto& pair) {
                return std::find(pair.second.begin(), pair.second.end(), key) != pair.second.end();
            });

        GeneratedObject* pEntry = m_mapInObjsToOutObjs.Exists(key);

        bool isClustered = (clusterIt != m_clusterMap.end());

        if (pEntry) {
            // If the entry already exists, update the index
            pEntry->index = i;
        }
        else {

            pEntry = m_mapInObjsToOutObjs.AddInput(key);

            if (pEntry) {
                AkAudioObjectID outputObjKey;

                if (isClustered) {

                    // Calculate and store the offset from original position to cluster centroid
                    pEntry->offset = clusterIt->first - inobj->positioning.threeD.xform.Position();

                    // Get the cluster index
                    int clusterIndex = std::distance(m_clusterMap.begin(), clusterIt);

                    // Check if an output object has already been created for this cluster
                    auto outputObjIt = clusterOutputObjects.find(clusterIndex);

                    if (outputObjIt == clusterOutputObjects.end()) {
                        // This is the first object in the cluster, so create a new output object
                        outputObjKey = m_utilities.CreateOutputObject(inobj, inObjects, i, m_pContext, &clusterIt->first);

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
                    pEntry->offset.Zero();
                    // If not clustered, create a unique output object
                    outputObjKey = m_utilities.CreateOutputObject(inobj, inObjects, i, m_pContext, nullptr);

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

void ObjectClusterFX::WriteToOutput(const AkAudioObjects& inObjects)
{ 
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

        m_utilities.ClearBuffers(outputObjects);

        // Iterate through our internal map.
        AkMixerInputMap<AkAudioObjectID,GeneratedObject>::Iterator it = m_mapInObjsToOutObjs.Begin();
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
                    if ((*it).pUserData->isClustered == true)
                    {
                        MixInputToOutput(inObj, inbuf, pBufferOut, inObj->cumulativeGain, (*it).pUserData);

                        // Find the the current cluster
                        AkAudioObjectID inputKey = inObj->key;
                        auto clusterIt = std::find_if(m_clusterMap.begin(), m_clusterMap.end(),
                            [&inputKey](const auto& pair) {
                                return std::find(pair.second.begin(), pair.second.end(), inputKey) != pair.second.end();
                            });

                        if (clusterIt != m_clusterMap.end())
                        {
                            // Set the output object's position
                            AkVector currentPos = inObj->positioning.threeD.xform.Position();
                            AkVector newPos = currentPos + (*it).pUserData->offset;
                            pObjOut->positioning.threeD.xform.SetPosition(newPos);

                            // Set the buffer's state
                            pBufferOut->eState = inbuf->eState != AK_NoMoreData ? inbuf->eState : AK_NoMoreData;

                            std::string objID = std::to_string((*it).pUserData->outputObjKey);
                            std::string objName = "Cluster" + objID;
                            pObjOut->SetName(m_pAllocator, objName.c_str());
                        }

                    }
                    else
                    {
                        // If not clustered, simply copy the input to the output.
                        m_utilities.CopyBuffer(inbuf, pBufferOut);

                        // Set the input object's position and state
                        pObjOut->positioning.threeD.xform.SetPosition(inObj->positioning.threeD.xform.Position());
                        pBufferOut->eState = inbuf->eState;

                        // Also, propagate the associated input object's custom metadata to the output.
                        pObjOut->arCustomMetadata.Copy(inObj->arCustomMetadata);

                        std::string objName = "Not clustered";
                        pObjOut->SetName(m_pAllocator, objName.c_str());
                    }

                    pBufferOut->uValidFrames = inbuf->uValidFrames;

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

void ObjectClusterFX::MixInputToOutput(const AkAudioObject* inObject, AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, GeneratedObject* pGeneratedObject)
{
    if (inBuffer->uValidFrames == 0 || inBuffer->NumChannels() == 0 || outBuffer->NumChannels() == 0) {
        return;
    }

    AkUInt32 uTransmixSize = AK::SpeakerVolumes::Matrix::GetRequiredSize(inBuffer->NumChannels(), outBuffer->NumChannels());

    AK::SpeakerVolumes::MatrixPtr currentVolumes = (AK::SpeakerVolumes::MatrixPtr)AkAllocaSIMD(uTransmixSize);
    AK::SpeakerVolumes::Matrix::Zero(currentVolumes, inBuffer->NumChannels(), outBuffer->NumChannels());

    AKRESULT result = m_pContext->GetMixerCtx()->ComputePositioning(
        inObject->positioning,
        inBuffer->GetChannelConfig(),
        outBuffer->GetChannelConfig(),
        currentVolumes
    );

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

void ObjectClusterFX::PopulateClusters(const AkAudioObjects& inObjects)
{
    std::vector<ObjectPosition> objectPositions;
    objectPositions.reserve(inObjects.uNumObjects);

    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inobj = inObjects.ppObjects[i];

        // Only 3D objects should be clustered.
        if (inobj->positioning.behavioral.spatMode != AK_SpatializationMode_None)
        {
            objectPositions.push_back({ inobj->positioning.threeD.xform.Position(), inobj->key });
        }
    }

    // Update Kmeans parameters
    m_kmeans.setDistanceThreshold(m_pParams->RTPC.distanceThreshold);
    m_kmeans.setTolerance(m_pParams->RTPC.tolerance);

    // Perform clustering only if there are objects
    m_clusterMap.clear();
    if (!objectPositions.empty()) {
        m_kmeans.performClustering(objectPositions);
        // Return cluster data
        m_clusterMap = m_kmeans.getClusters();
    }
}

AKRESULT ObjectClusterFX::AllocateVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix, AkUInt32 in_uNumChannelsIn, AkUInt32 in_uNumChannelsOut)
{
    AkUInt32 size = AK::SpeakerVolumes::Matrix::GetRequiredSize(in_uNumChannelsIn, in_uNumChannelsOut);
    volumeMatrix = (AK::SpeakerVolumes::MatrixPtr)m_pAllocator->Malloc(
        size,
        __FILE__,
        __LINE__
    );
    return volumeMatrix ? AK_Success : AK_InsufficientMemory;
}
