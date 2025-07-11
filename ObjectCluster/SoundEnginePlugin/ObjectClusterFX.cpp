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
    FreeAllVolumes();
}

AKRESULT ObjectClusterFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{
    m_pParams = (ObjectClusterFXParams*)in_pParams;
    m_mapInObjsToOutObjs.Init(in_pAllocator);
    m_pContext = in_pContext;
    m_pAllocator = in_pAllocator;

    in_rFormat.channelConfig.SetObject();

    // Set min-max values for the distance threshold
    // These are specific to eve & frontier ships, adjust accordingly
    m_kmeans->setMinDistanceThreshold(1.f);
    m_kmeans->setMaxDistanceThreshold(1000.f);

    return AK_Success;
}

AKRESULT ObjectClusterFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    FreeAllVolumes();

    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT ObjectClusterFX::Reset()
{
    FreeAllVolumes();
    m_clusters.clear();
    m_tempBuffers.clear();
    m_tempObjects.clear();

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
    const AkAudioObjects& inObjects, 
    const AkAudioObjects& outObjects
) {
    AKASSERT(inObjects.uNumObjects > 0);

    PrepareAudioObjects(inObjects);
    ProcessAudioObjects(inObjects);
    UpdateClusterPositions(inObjects);
}

void ObjectClusterFX::ForceReclustering()
{
	m_clusters.clear();

    auto it = m_mapInObjsToOutObjs.Begin();
    while (it != m_mapInObjsToOutObjs.End()) {
        if ((*it).pUserData && (*it).pUserData->isClustered) {
            (*it).pUserData->isClustered = false;
        }
        ++it;
    }

	m_kmeans->reset();
}

void ObjectClusterFX::PrepareAudioObjects(const AkAudioObjects& inObjects)
{
    FeedPositionsToKMeans(inObjects);
    std::unordered_map<const std::pair<AkVector, std::vector<AkAudioObjectID>>*, AkAudioObjectID> clusterOutputObjects;

    // Get current outputs at start
    AkAudioObjects existingOutputs = GetCurrentOutputObjects();

    // Update existing objects and map clusters to existing outputs
    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inobj = inObjects.ppObjects[i];
        AkAudioObjectID key = inobj->key;
        GeneratedObject* pEntry = m_mapInObjsToOutObjs.Exists(key);
        if (pEntry) {
            pEntry->index = i;
        }
    }

    // For each cluster, try to find if any of its objects are using an existing output
    if (existingOutputs.uNumObjects > 0) {
        for (const auto& cluster : m_clusters) {
            for (const auto& objId : cluster.second) {
                GeneratedObject* pEntry = m_mapInObjsToOutObjs.Exists(objId);
                if (pEntry && pEntry->isClustered) {
                    // Verify this output still exists
                    for (AkUInt32 i = 0; i < existingOutputs.uNumObjects; i++) {
                        if (existingOutputs.ppObjects[i] &&
                            existingOutputs.ppObjects[i]->key == pEntry->outputObjKey) {
                            clusterOutputObjects[&cluster] = pEntry->outputObjKey;
                            break;
                        }
                    }
                    if (clusterOutputObjects.find(&cluster) != clusterOutputObjects.end()) {
                        break;  // Found valid output for this cluster
                    }
                }
            }
        }
    }

    // Handle new objects
    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inobj = inObjects.ppObjects[i];
        AkAudioObjectID key = inobj->key;

        GeneratedObject* pEntry = m_mapInObjsToOutObjs.Exists(key);
        if (!pEntry) {  // Only handle new objects
            pEntry = m_mapInObjsToOutObjs.AddInput(key);
            if (!pEntry) continue;

            bool isPositionedObject = (inobj->positioning.behavioral.spatMode == AK_SpatializationMode_PositionOnly ||
                inobj->positioning.behavioral.spatMode == AK_SpatializationMode_PositionAndOrientation);

            if (isPositionedObject) {
                // Find which cluster this object belongs to from KMeans results
                const std::pair<AkVector, std::vector<AkAudioObjectID>>* assignedCluster = nullptr;
                for (const auto& cluster : m_clusters) {
                    auto it = std::find(cluster.second.begin(), cluster.second.end(), key);
                    if (it != cluster.second.end()) {
                        assignedCluster = &cluster;
                        break;
                    }
                }

                if (assignedCluster) {
                    // Check if we have an existing output for this cluster
                    auto outputObjIt = clusterOutputObjects.find(assignedCluster);
                    if (outputObjIt != clusterOutputObjects.end()) {
                        // Use existing cluster output
                        pEntry->outputObjKey = outputObjIt->second;
                        pEntry->isClustered = true;
                    }
                    else {
                        // Create new output for this cluster
                        pEntry->outputObjKey = m_utilities->CreateOutputObject(inobj, inObjects, i, m_pContext, &assignedCluster->first);
                        clusterOutputObjects[assignedCluster] = pEntry->outputObjKey;
                        pEntry->isClustered = true;
                    }
                }
                else {
                    // Try to find nearest existing cluster
                    AkAudioObjectID bestClusterKey;
                    if (FindBestCluster(inobj->positioning.threeD.xform.Position(), existingOutputs, bestClusterKey) == AK_Success) {
                        pEntry->outputObjKey = bestClusterKey;
                        pEntry->isClustered = true;
                    }
                    else {
                        // Only mark as unclustered if we really can't find a suitable cluster
                        pEntry->outputObjKey = m_utilities->CreateOutputObject(inobj, inObjects, i, m_pContext, nullptr);
                        pEntry->isClustered = false;
                    }
                }
            }
            else {
                // Not a spatial object - create unclustered output
                pEntry->outputObjKey = m_utilities->CreateOutputObject(inobj, inObjects, i, m_pContext, nullptr);
                pEntry->isClustered = false;
            }
            pEntry->index = i;
        }
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

                if ((*it).pUserData && (*it).pUserData->volumeMatrix) {
                    FreeVolume((*it).pUserData->volumeMatrix);
                }
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
    const ClusterState& clusterState)
{

    if (inBuf->uValidFrames > 0) {
        MixToCluster(inObj, inBuf, outBuf, inObj->cumulativeGain, userData);
    }

    // Set clustered object's name
    std::string objName = "Cluster" + std::to_string(userData->outputObjKey);
    outObj->SetName(m_pAllocator, objName.c_str());

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

    if (outObj == nullptr || inBuf == nullptr || outBuf == nullptr) {
        return;
    }

    m_utilities->CopyBuffer(inBuf, outBuf);
    outObj->positioning.threeD.xform.SetPosition(inObj->positioning.threeD.xform.Position());

    outBuf->eState = inBuf->eState;
    outBuf->uValidFrames = inBuf->uValidFrames;
    outObj->SetName(m_pAllocator, "Not clustered");
}

void ObjectClusterFX::MixToCluster(const AkAudioObject* inObject, AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, GeneratedObject* pGeneratedObject)
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

    AKPLATFORM::AkMemCpy(pGeneratedObject->volumeMatrix, currentVolumes, uTransmixSize);
}

void ObjectClusterFX::FeedPositionsToKMeans(const AkAudioObjects& inObjects) {
    // Check for threshold change before normal processing
    if (m_lastDistanceThreshold != m_pParams->RTPC.distanceThreshold) {
        m_kmeans->setDistanceThreshold(m_pParams->RTPC.distanceThreshold);
        m_lastDistanceThreshold = m_pParams->RTPC.distanceThreshold;
        ForceReclustering();
    }

    std::vector<ObjectPosition> objectPositions;
    objectPositions.reserve(inObjects.uNumObjects);

    // Check if objects should be clustered based on spatialization mode
    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inobj = inObjects.ppObjects[i];

        bool shouldCluster = (inobj->positioning.behavioral.spatMode == AK_SpatializationMode_PositionOnly ||
            inobj->positioning.behavioral.spatMode == AK_SpatializationMode_PositionAndOrientation);

        if (shouldCluster) {
            objectPositions.push_back({ inobj->positioning.threeD.xform.Position(), inobj->key });
        }
    }

    // Perform clustering only if there are objects
    m_clusters.clear();
    if (!objectPositions.empty()) {
        m_kmeans->performClustering(objectPositions);

        auto tempClusters = m_kmeans->getClusters();
        m_clusters.reserve(tempClusters.size());

        for (const auto& pair : tempClusters) {
            m_clusters.emplace_back(pair.first, pair.second);
        }
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

const std::pair<AkVector, std::vector<AkAudioObjectID>>* ObjectClusterFX::GetCluster(AkAudioObjectID objectId) const
{
    for (const auto& cluster : m_clusters) {
        for (const auto& id : cluster.second) {
            if (id == objectId) {
                return &cluster;
            }
        }
    }
    return nullptr;
}

void ObjectClusterFX::FreeVolume(AK::SpeakerVolumes::MatrixPtr& volumeMatrix)
{
    if (volumeMatrix) {
        m_pAllocator->Free(volumeMatrix);
        volumeMatrix = nullptr;
    }
}

void ObjectClusterFX::FreeAllVolumes()
{
    auto it = m_mapInObjsToOutObjs.Begin();
    while (it != m_mapInObjsToOutObjs.End()) {
        if ((*it).pUserData && (*it).pUserData->volumeMatrix) {
            FreeVolume((*it).pUserData->volumeMatrix);
        }
        ++it;
    }
}

void ObjectClusterFX::UpdateClusterPositions(const AkAudioObjects& inObjects)
{
    AkAudioObjects outputObjects = GetCurrentOutputObjects();
    if (outputObjects.uNumObjects == 0) return;

    // Track which clusters we've already processed to avoid duplicates
    std::set<AkAudioObjectID> processedClusters;

    auto it = m_mapInObjsToOutObjs.Begin();
    while (it != m_mapInObjsToOutObjs.End()) {
        if ((*it).pUserData && (*it).pUserData->isClustered) {
            AkAudioObjectID clusterKey = (*it).pUserData->outputObjKey;

            if (processedClusters.find(clusterKey) == processedClusters.end()) {
                // Find the corresponding cluster
                const std::pair<AkVector, std::vector<AkAudioObjectID>>* cluster = GetCluster((*it).key);
                if (cluster) {
                    AkVector meanPosition = m_utilities->CalculateMeanPosition(cluster->second, inObjects);

                    // Find and update the output object for this cluster
                    for (AkUInt32 i = 0; i < outputObjects.uNumObjects; i++) {
                        if (outputObjects.ppObjects[i] &&
                            outputObjects.ppObjects[i]->key == clusterKey) {
                            outputObjects.ppObjects[i]->positioning.threeD.xform.SetPosition(meanPosition);
                        }
                    }
                    processedClusters.insert(clusterKey);
                }
            }
        }
        ++it;
    }
}

AKRESULT ObjectClusterFX::FindBestCluster(const AkVector& position, const AkAudioObjects& existingOutputs, AkAudioObjectID& outClusterKey)
{
    const float thresholdSquared = m_pParams->RTPC.distanceThreshold * m_pParams->RTPC.distanceThreshold;
    float closestDistanceSquared = thresholdSquared;
    outClusterKey = AK_INVALID_AUDIO_OBJECT_ID;

    for (AkUInt32 i = 0; i < existingOutputs.uNumObjects; ++i)
    {
        const AkAudioObject* outObj = existingOutputs.ppObjects[i];
        if (!outObj) continue;

        const AkVector outObjPos = outObj->positioning.threeD.xform.Position();

        // Check all possible matches for this output object
        auto existingEntry = m_mapInObjsToOutObjs.Begin();
        while (existingEntry != m_mapInObjsToOutObjs.End())
        {
            const GeneratedObject* userData = (*existingEntry).pUserData;
            if (userData &&
                userData->isClustered &&
                userData->outputObjKey == outObj->key)  // Found matching cluster
            {
                const float distanceSquared = m_utilities->GetDistanceSquared(outObjPos, position);
                if (distanceSquared < closestDistanceSquared) {
                    closestDistanceSquared = distanceSquared;
                    outClusterKey = outObj->key;
                }
            }
            ++existingEntry;
        }
    }

    return (outClusterKey != AK_INVALID_AUDIO_OBJECT_ID) ? AK_Success : AK_Fail;
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

            if (inBuf->eState != AK_NoMoreData) {
                state.activeInputCount++;
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
    AkAudioObjects outputObjects{};
    outputObjects.uNumObjects = 0;
    outputObjects.ppObjectBuffers = nullptr;
    outputObjects.ppObjects = nullptr;

    if (!m_pContext) {
        return outputObjects;
    }

    // First call to get count
    m_pContext->GetOutputObjects(outputObjects);

    if (outputObjects.uNumObjects > 0) {
        try {
            m_tempBuffers.resize(outputObjects.uNumObjects);
            m_tempObjects.resize(outputObjects.uNumObjects);

            outputObjects.ppObjectBuffers = m_tempBuffers.data();
            outputObjects.ppObjects = m_tempObjects.data();

            // Second call to get actual data
            m_pContext->GetOutputObjects(outputObjects);
        }
        catch (const std::bad_alloc&) {
            // Reset on allocation failure
            outputObjects.uNumObjects = 0;
            outputObjects.ppObjectBuffers = nullptr;
            outputObjects.ppObjects = nullptr;
        }
    }

    return outputObjects;
}
