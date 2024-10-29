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
    , m_kmeans(std::make_unique<KMeans>())
    , m_utilities(std::make_unique<Utilities>())
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

    in_rFormat.channelConfig.SetObject();

    return AK_Success;
}

AKRESULT ObjectClusterFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT ObjectClusterFX::Reset()
{
    m_clusterMap.clear();
    m_tempBuffers.clear();
    m_tempObjects.clear();
    m_activeClusters.clear();

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
    ProcessAudioObjects(inObjects);
}

void ObjectClusterFX::BookkeepAudioObjects(const AkAudioObjects& inObjects)
{
    PopulateClusters(inObjects);

    // Get existing outputs once at the beginning
    AkAudioObjects existingOutputs = { 0 };
    if (!m_mapInObjsToOutObjs.IsEmpty()) {
        existingOutputs = GetCurrentOutputObjects();
    }

    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i)
    {
        AkAudioObject* inobj = inObjects.ppObjects[i];
        AkAudioObjectID key = inobj->key;

        // Check if this object belongs to any cluster
        auto clusterIt = std::find_if(m_clusterMap.begin(), m_clusterMap.end(),
            [&key](const auto& pair) {
                return std::find(pair.second.begin(), pair.second.end(), key) != pair.second.end();
            });

        GeneratedObject* pEntry = m_mapInObjsToOutObjs.Exists(key);
        bool isClustered = (clusterIt != m_clusterMap.end());

        if (pEntry)
        {
            // Object already exists, just update its index
            pEntry->index = i;
            continue;
        }

        // Create new entry for this object
        pEntry = m_mapInObjsToOutObjs.AddInput(key);
        if (!pEntry) continue;

        AkAudioObjectID outputObjKey = AK_INVALID_AUDIO_OBJECT_ID;

        if (isClustered)
        {
            // Try to find the best existing cluster first
            if (existingOutputs.uNumObjects > 0) {
                AKRESULT result = FindBestCluster(clusterIt->first, existingOutputs, outputObjKey);

                // If no suitable existing cluster found, create a new one
                if (result != AK_Success) {
                    outputObjKey = m_utilities->CreateOutputObject(inobj, inObjects, i, m_pContext, &clusterIt->first);
                }
            }
            else {
                // No existing clusters, create first one
                outputObjKey = m_utilities->CreateOutputObject(inobj, inObjects, i, m_pContext, &clusterIt->first);
            }

            pEntry->offset = clusterIt->first - inobj->positioning.threeD.xform.Position();
            pEntry->outputObjKey = outputObjKey;
            pEntry->isClustered = true;
        }
        else
        {
            // Handle unclustered object
            pEntry->offset.Zero();
            outputObjKey = m_utilities->CreateOutputObject(inobj, inObjects, i, m_pContext, nullptr);

            if (outputObjKey != AK_INVALID_AUDIO_OBJECT_ID)
            {
                pEntry->outputObjKey = outputObjKey;
                pEntry->isClustered = false;
            }
        }
        pEntry->index = i;
    }

    m_tempBuffers.clear();
    m_tempObjects.clear();
}

void ObjectClusterFX::ProcessAudioObjects(const AkAudioObjects& inObjects)
{
    if (inObjects.uNumObjects == 0) {
        return;
    }

    AkAudioObjects outputObjects = GetCurrentOutputObjects();

    if (outputObjects.uNumObjects > 0) {
        auto clusterStates = ReadClusterStates(inObjects);
        m_utilities->ClearBuffers(outputObjects);

        auto it = m_mapInObjsToOutObjs.Begin();
        while (it != m_mapInObjsToOutObjs.End()) {
            if ((*it).pUserData && (*it).pUserData->index >= 0) {
                AkAudioObject* inObj = inObjects.ppObjects[(*it).pUserData->index];
                AkAudioBuffer* inBuf = inObjects.ppObjectBuffers[(*it).pUserData->index];

                // Find matching output object
                AkAudioObject* outObj = nullptr;
                AkAudioBuffer* outBuf = nullptr;

                for (AkUInt32 i = 0; i < outputObjects.uNumObjects; i++) {
                    if (outputObjects.ppObjects[i] &&
                        (*it).pUserData->outputObjKey != AK_INVALID_AUDIO_OBJECT_ID &&
                        outputObjects.ppObjects[i]->key == (*it).pUserData->outputObjKey) {
                        outObj = outputObjects.ppObjects[i];
                        outBuf = outputObjects.ppObjectBuffers[i];
                        break;
                    }
                }

                // Process if output was found
                if (outObj && outBuf) {
                    if ((*it).pUserData->isClustered) {
                        ProcessClusteredObject(
                            inObj,
                            inBuf,
                            outObj,
                            outBuf,
                            (*it).pUserData,
                            clusterStates[(*it).pUserData->outputObjKey]);
                    }
                    else {
                        ProcessUnclustered(
                            inObj,
                            inBuf,
                            outObj,
                            outBuf);
                    }
                }

                (*it).pUserData->index = -1;
                ++it;
            }
            else {
                it = m_mapInObjsToOutObjs.EraseSwap(it);
            }
        }
    }

    m_tempBuffers.clear();
    m_tempObjects.clear();
}

void ObjectClusterFX::ProcessClusteredObject(
    const AkAudioObject* inObj,
    AkAudioBuffer* inBuf,
    AkAudioObject* outObj,
    AkAudioBuffer* outBuf,
    GeneratedObject* userData,
    const ClusterState& clusterState) {

    if (inBuf->uValidFrames > 0) {
        MixInputToOutput(inObj, inBuf, outBuf, inObj->cumulativeGain, userData);
    }

    // Update position
    auto clusterIt = std::find_if(m_clusterMap.begin(), m_clusterMap.end(),
        [&inObj](const auto& pair) {
            return std::find(pair.second.begin(), pair.second.end(), inObj->key) != pair.second.end();
        });

    if (clusterIt != m_clusterMap.end()) {
        // Calculate new position using current position and stored offset
        AkVector currentPos = inObj->positioning.threeD.xform.Position();
        AkVector newPos = currentPos + userData->offset;
        outObj->positioning.threeD.xform.SetPosition(newPos);

        std::string objName = "Cluster" + std::to_string(userData->outputObjKey);
        outObj->SetName(m_pAllocator, objName.c_str());
    }

    // Update buffer state
    bool allInputsDone = (clusterState.activeInputCount == 0);
    bool hasValidFrames = (clusterState.maxFrames > 0);

    outBuf->eState = (allInputsDone && !hasValidFrames) ? AK_NoMoreData : AK_DataReady;
    outBuf->uValidFrames = hasValidFrames ? clusterState.maxFrames : 0;
}

void ObjectClusterFX::ProcessUnclustered(
    const AkAudioObject* inObj,
    AkAudioBuffer* inBuf,
    AkAudioObject* outObj,
    AkAudioBuffer* outBuf) {

    m_utilities->CopyBuffer(inBuf, outBuf);
    outObj->positioning.threeD.xform.SetPosition(inObj->positioning.threeD.xform.Position());
    outObj->arCustomMetadata.Copy(inObj->arCustomMetadata);
    outBuf->eState = inBuf->eState;
    outBuf->uValidFrames = inBuf->uValidFrames;
    outObj->SetName(m_pAllocator, "Not clustered");
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
    m_kmeans->setDistanceThreshold(m_pParams->RTPC.distanceThreshold);
    m_kmeans->setTolerance(m_pParams->RTPC.tolerance);

    // Perform clustering only if there are objects
    m_clusterMap.clear();
    if (!objectPositions.empty()) {
        m_kmeans->performClustering(objectPositions);
        // Return cluster data
        m_clusterMap = m_kmeans->getClusters();
    }
}

AKRESULT ObjectClusterFX::AllocateVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix,
    AkUInt32 in_uNumChannelsIn,
    AkUInt32 in_uNumChannelsOut)
{
    AkUInt32 size = AK::SpeakerVolumes::Matrix::GetRequiredSize(in_uNumChannelsIn, in_uNumChannelsOut);
    float* matrix = static_cast<float*>(m_pAllocator->Malloc(size, __FILE__, __LINE__));

    if (!matrix) {
        return AK_InsufficientMemory;
    }

    volumeMatrix = matrix;
    return AK_Success;
}

void ObjectClusterFX::FreeVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix)
{
    if (volumeMatrix) {
        m_pAllocator->Free(volumeMatrix);
        volumeMatrix = nullptr;
    }
}

AKRESULT ObjectClusterFX::FindBestCluster(const AkVector& position, const AkAudioObjects& existingOutputs, AkAudioObjectID& outClusterKey)
{
    float thresholdSquared = m_pParams->RTPC.distanceThreshold * m_pParams->RTPC.distanceThreshold;
    float closestDistanceSquared = FLT_MAX;
    bool foundCluster = false;

    // First pass: find the closest existing cluster within threshold
    for (AkUInt32 i = 0; i < existingOutputs.uNumObjects; ++i) {
        auto existingEntry = m_mapInObjsToOutObjs.Begin();
        while (existingEntry != m_mapInObjsToOutObjs.End()) {
            if ((*existingEntry).pUserData->isClustered &&
                (*existingEntry).pUserData->outputObjKey == existingOutputs.ppObjects[i]->key) {

                AkVector existingPos = existingOutputs.ppObjects[i]->positioning.threeD.xform.Position();
                float distanceSquared = m_utilities->GetDistanceSquared(existingPos, position);

                if (distanceSquared < thresholdSquared && distanceSquared < closestDistanceSquared) {
                    closestDistanceSquared = distanceSquared;
                    outClusterKey = existingOutputs.ppObjects[i]->key;
                    foundCluster = true;
                }
            }
            ++existingEntry;
        }
    }

    return foundCluster ? AK_Success : AK_Fail;
}

std::unordered_map<AkAudioObjectID, ClusterState> ObjectClusterFX::ReadClusterStates(const AkAudioObjects& inObjects)
{
    std::unordered_map<AkAudioObjectID, ClusterState> clusterStates;

    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inObj = inObjects.ppObjects[i];
        AkAudioBuffer* inBuf = inObjects.ppObjectBuffers[i];

        auto it = m_mapInObjsToOutObjs.Exists(inObj->key);
        if (it && it->isClustered) {
            AkAudioObjectID clusterID = it->outputObjKey;
            auto& state = clusterStates[clusterID];

            state.totalInputCount++;
            if (inBuf->eState != AK_NoMoreData) {
                state.activeInputCount++;
                state.hasActiveInput = true;
            }
            if (inBuf->uValidFrames > 0) {
                state.maxFrames = std::max(state.maxFrames, inBuf->uValidFrames);
            }
        }
    }

    return clusterStates;
}

AkAudioObjects ObjectClusterFX::GetCurrentOutputObjects()
{
    AkAudioObjects outputObjects;
    outputObjects.uNumObjects = 0;
    outputObjects.ppObjectBuffers = nullptr;
    outputObjects.ppObjects = nullptr;

    // First call to get count
    m_pContext->GetOutputObjects(outputObjects);

    if (outputObjects.uNumObjects > 0)
    {
        // Resize our member vectors
        m_tempBuffers.resize(outputObjects.uNumObjects);
        m_tempObjects.resize(outputObjects.uNumObjects);

        // Use the vectors' data
        outputObjects.ppObjectBuffers = m_tempBuffers.data();
        outputObjects.ppObjects = m_tempObjects.data();

        // Second call to get actual data
        m_pContext->GetOutputObjects(outputObjects);
    }

    return outputObjects;
}
