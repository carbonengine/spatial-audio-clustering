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
  Copyright (c) 2024 CCP ehf.

This implementation was developed by CCP Games for spatial audio object clustering
in EVE Online and EVE Frontier. This implementation does not grant any rights to
CCP's trademarks or game content. EVE Online and EVE Frontier are registered
trademarks of CCP ehf.
*******************************************************************************/

#include "SpatialClusteringFX.h"
#include "../SpatialClusteringConfig.h"

#include <AK/AkWwiseSDKVersion.h>
#include <AK/SoundEngine/Common/AkHashTableFuncs.h>
#include <AK/Tools/Common/AkMurMurHash.h>
#include <AK/Tools/Common/AkPlatformFuncs.h>

#include <cmath>

AK::IAkPlugin* CreateSpatialClusteringFX(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, SpatialClusteringFX());
}

AK::IAkPluginParam* CreateSpatialClusteringFXParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, SpatialClusteringFXParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(SpatialClusteringFX, AkPluginTypeEffect, SpatialClusteringConfig::CompanyID, SpatialClusteringConfig::PluginID)

namespace
{
    /** @brief Number of inputs and outputs that Init reserves memory for. */
    const AkUInt32 kInitialCapacity = 64;

    /** @brief Time constant in seconds of the input loudness smoothing. */
    const AkReal32 kLoudnessTimeConstant = 0.05f;

    /** @brief Minimum time in seconds between two monitor messages of the same kind. */
    const AkReal32 kMessageIntervalSeconds = 5.f;

    /** @brief Angular radius in degrees of a cluster whose spread is 1, since a spread of 0.5 covers 180 degrees. */
    const AkReal32 kFullSpreadRadius = 180.f;

    /** @brief Name of passthrough outputs whose input has no name. */
    const char kPassthroughName[] = "Passthrough";

    /** @brief Start of the name of cluster outputs. */
    const char kClusterNamePrefix[] = "Cluster ";

    /** @brief Message posted when the plug-in is not on a bus. */
    const char kNotOnBusMessage[] = "Spatial Clustering only runs on busses. Insert it on an Audio Objects bus.";

    /** @brief Message posted when the bus is not an Audio Objects bus. */
    const char kNotObjectBusMessage[] = "Spatial Clustering is not on an Audio Objects bus, so there is nothing to cluster and the bus signal passes through. Set the bus channel configuration to Audio Objects.";

    /** @brief Message posted when a plug-in service is missing. */
    const char kMissingServiceMessage[] = "Spatial Clustering could not get the hash table service from the sound engine.";

    /** @brief Message posted when output objects cannot be created. */
    const char kCreateFailedMessage[] = "Spatial Clustering could not create output objects, so some inputs are silent.";

    /** @brief Message posted when memory runs out. */
    const char kOutOfMemoryMessage[] = "Spatial Clustering ran out of memory, so some audio may drop out.";

    /** @brief Message posted when the inputs that are left cannot be merged down to Max Outputs. */
    const char kOverBudgetMessage[] = "Spatial Clustering needs more outputs than Max Outputs allows because the remaining inputs cannot be merged (passthrough inputs, excluded inputs or different custom metadata).";

    /** @brief Returns a value limited to a range. */
    template <typename T>
    T Clamp(T in_value, T in_min, T in_max)
    {
        return in_value < in_min ? in_min : (in_value > in_max ? in_max : in_value);
    }

    /** @brief Converts a time in milliseconds to a number of frames, rounded to the nearest frame. */
    AkUInt32 MillisecondsToFrames(AkReal32 in_fMilliseconds, AkReal32 in_fFrameSeconds)
    {
        return static_cast<AkUInt32>(in_fMilliseconds * 0.001f / in_fFrameSeconds + 0.5f);
    }

    /** @brief Largest number of channels of an input that can be clustered. */
    constexpr AkUInt32 kMaxClusterableChannels = 2;

    /** @brief Returns true for mono and stereo inputs that are fully spatialized in 3D. */
    bool IsClusterable(const AkAudioObject& in_object, const AkAudioBuffer& in_buffer)
    {
        const AkBehavioralPositioningData& behavioral = in_object.positioning.behavioral;
        const Ak3DSpatializationMode spatMode = behavioral.spatMode;
        const AkUInt32 uNumChannels = in_buffer.NumChannels();
        return uNumChannels >= 1 && uNumChannels <= kMaxClusterableChannels
            && (spatMode == AK_SpatializationMode_PositionOnly || spatMode == AK_SpatializationMode_PositionAndOrientation)
            && behavioral.panSpatMix >= 1.f;
    }

    /**
     * @brief Returns the gain applied to each channel of an input when it is mixed down to a
     * mono cluster output: 1 for mono and -3 dB for stereo, the Wwise stereo to mono downmix.
     */
    AkReal32 DownmixGain(AkUInt32 in_uNumChannels)
    {
        return in_uNumChannels > 1 ? 1.f / std::sqrt(static_cast<AkReal32>(in_uNumChannels)) : 1.f;
    }

    /** @brief Returns the RMS level of the mono downmix of a buffer over the valid frames. */
    AkReal32 ComputeRms(AkAudioBuffer& in_buffer)
    {
        const AkUInt32 uFrames = in_buffer.uValidFrames;
        const AkUInt32 uNumChannels = in_buffer.NumChannels();
        if (uFrames == 0 || uNumChannels == 0)
            return 0.f;

        const AkReal32 fGain = DownmixGain(uNumChannels);
        AkReal32 fSum = 0.f;
        for (AkUInt32 i = 0; i < uFrames; ++i)
        {
            AkReal32 fSample = 0.f;
            for (AkUInt32 channel = 0; channel < uNumChannels; ++channel)
                fSample += in_buffer.GetChannel(channel)[i];
            fSample *= fGain;
            fSum += fSample * fSample;
        }

        const AkReal32 fRms = std::sqrt(fSum / static_cast<AkReal32>(uFrames));
        return std::isfinite(fRms) ? fRms : 0.f;
    }

    /** @brief Returns a hash of the custom metadata plug-in types that does not depend on their order, or 0 when there is none. */
    AkUInt32 HashCustomMetadata(const AkAudioObject& in_object)
    {
        const AkAudioObject::ArrayCustomMetadata& metadata = in_object.arCustomMetadata;
        if (metadata.IsEmpty())
            return 0;

        AkUInt64 uSum = 0;
        for (AkAudioObject::ArrayCustomMetadata::Iterator it = metadata.Begin(); it != metadata.End(); ++it)
            uSum += AkHashMurMurMix64(static_cast<AkUInt64>((*it).pluginID));

        const AkUInt32 uHash = static_cast<AkUInt32>(uSum ^ (uSum >> 32));
        return uHash != 0 ? uHash : 1;
    }

    /** @brief Adds one channel to another with a gain that ramps linearly over the frames. */
    void MixRamp(const AkSampleType* in_pInput, AkSampleType* io_pOutput, AkUInt32 in_uFrames, AkReal32 in_fGainStart, AkReal32 in_fGainEnd)
    {
        if (in_uFrames == 0)
            return;

        const AkReal32 fStep = (in_fGainEnd - in_fGainStart) / static_cast<AkReal32>(in_uFrames);
        for (AkUInt32 i = 0; i < in_uFrames; ++i)
            io_pOutput[i] += in_pInput[i] * (in_fGainStart + fStep * static_cast<AkReal32>(i));
    }

    /** @brief Writes "Cluster <id>" into a character array. */
    template <AkUInt32 N>
    void FormatClusterName(AkUInt32 in_uId, char (&out_szName)[N])
    {
        AkUInt32 uLength = 0;
        for (const char* pChar = kClusterNamePrefix; *pChar != '\0' && uLength + 1 < N; ++pChar)
            out_szName[uLength++] = *pChar;

        char aDigits[10];
        AkUInt32 uNumDigits = 0;
        do
        {
            aDigits[uNumDigits++] = static_cast<char>('0' + in_uId % 10);
            in_uId /= 10;
        } while (in_uId != 0);

        while (uNumDigits > 0 && uLength + 1 < N)
            out_szName[uLength++] = aDigits[--uNumDigits];

        out_szName[uLength] = '\0';
    }

    /** @brief Returns the slot of a key in a hash table, or -1 when the key is missing. */
    template <typename ValueType>
    AkInt32 FindSlot(const AK::AkHashTable<AkUInt64, ValueType>& in_table, AkUInt64 in_uKey)
    {
        return AK::HashTable::GetFirstSlotForKey<AkUInt64>(&in_table, in_uKey);
    }

    /** @brief Returns true if a hash table holds memory. */
    bool HasMemory(const AK::AkHashTableBase<AkUInt64>& in_table)
    {
        return in_table.pKeys != nullptr || in_table.uNumReservedEntries != 0;
    }
}

void SpatialClusteringAllocator::Init(AK::IAkPluginMemAlloc* in_pAllocator)
{
    m_pAllocator = in_pAllocator;
}

void* SpatialClusteringAllocator::Alloc(size_t in_size)
{
    if (m_pAllocator == nullptr)
        return nullptr;

    return AK_PLUGIN_ALLOC(m_pAllocator, in_size);
}

void SpatialClusteringAllocator::Free(void* in_pBlock)
{
    if (m_pAllocator != nullptr && in_pBlock != nullptr)
        AK_PLUGIN_FREE(m_pAllocator, in_pBlock);
}

SpatialClusteringFX::SpatialClusteringFX()
    : m_pParams(nullptr)
    , m_pAllocator(nullptr)
    , m_pContext(nullptr)
    , m_pPriorityService(nullptr)
    , m_pHashTableService(nullptr)
    , m_bEngineInitialized(false)
    , m_fSpreadFromSize(1.f)
    , m_uSampleRate(0)
    , m_uParamsMaxFrames(0)
    , m_uMessageInterval(1)
    , m_uFrame(0)
    , m_uNumOutputs(0)
    , m_aMessageFrames{}
    , m_aMessagePosted{}
{
}

SpatialClusteringFX::~SpatialClusteringFX()
{
}

AKRESULT SpatialClusteringFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& io_rFormat)
{
    m_pParams = static_cast<SpatialClusteringFXParams*>(in_pParams);
    m_pAllocator = in_pAllocator;
    m_pContext = in_pContext;

    if (in_pContext->GetMixerCtx() == nullptr)
    {
        in_pContext->PostMonitorMessage(kNotOnBusMessage, AK::Monitor::ErrorLevel_Error);
        return AK_Fail;
    }

    if (io_rFormat.channelConfig.eConfigType != AK_ChannelConfigType_Objects)
        in_pContext->PostMonitorMessage(kNotObjectBusMessage, AK::Monitor::ErrorLevel_Error);

    io_rFormat.channelConfig.SetObject();

    AK::IAkGlobalPluginContext* pGlobalContext = in_pContext->GlobalContext();
    m_pPriorityService = AK_GET_PLUGIN_SERVICE_AUDIO_OBJECT_PRIORITY(pGlobalContext);
    m_pHashTableService = AK_GET_PLUGIN_SERVICE_HASHTABLE(pGlobalContext);
    if (m_pHashTableService == nullptr)
    {
        in_pContext->PostMonitorMessage(kMissingServiceMessage, AK::Monitor::ErrorLevel_Error);
        return AK_Fail;
    }

    m_uSampleRate = pGlobalContext->GetSampleRate();
    if (m_uSampleRate == 0)
        m_uSampleRate = io_rFormat.uSampleRate;

    m_inputs.Init(in_pAllocator);
    m_inputPriorities.Init(in_pAllocator);
    m_requests.Init(in_pAllocator);
    m_newObjects.Init(in_pAllocator);
    m_outputObjects.Init(in_pAllocator);
    m_outputBuffers.Init(in_pAllocator);
    m_clusterOutputs.Init(in_pAllocator);
    m_clusterValidFrames.Init(in_pAllocator);
    m_clusterHasData.Init(in_pAllocator);
    m_outputWritten.Init(in_pAllocator);
    m_priorityObjects.Init(in_pAllocator);
    m_priorityValues.Init(in_pAllocator);

    const bool bArraysReserved = m_inputs.Reserve(kInitialCapacity) == AK_Success
        && m_inputPriorities.Reserve(kInitialCapacity) == AK_Success
        && m_requests.Reserve(kInitialCapacity) == AK_Success
        && m_newObjects.Reserve(kInitialCapacity) == AK_Success
        && m_outputObjects.Reserve(kInitialCapacity + 1) == AK_Success
        && m_outputBuffers.Reserve(kInitialCapacity + 1) == AK_Success
        && m_clusterOutputs.Reserve(kInitialCapacity) == AK_Success
        && m_clusterValidFrames.Reserve(kInitialCapacity) == AK_Success
        && m_clusterHasData.Reserve(kInitialCapacity) == AK_Success
        && m_outputWritten.Reserve(kInitialCapacity + 1) == AK_Success
        && m_priorityObjects.Reserve(kInitialCapacity) == AK_Success
        && m_priorityValues.Reserve(kInitialCapacity) == AK_Success;

    const bool bTablesReserved = bArraysReserved
        && m_pHashTableService->Init(&m_passthroughs, in_pAllocator, kInitialCapacity) == AK_Success
        && m_pHashTableService->Init(&m_clusters, in_pAllocator, kInitialCapacity) == AK_Success
        && m_pHashTableService->Init(&m_outputIndices, in_pAllocator, kInitialCapacity) == AK_Success;

    m_engineAllocator.Init(in_pAllocator);
    m_bEngineInitialized = bTablesReserved;
    if (!bTablesReserved || !m_engine.Init(&m_engineAllocator, kInitialCapacity))
    {
        ReleaseMemory();
        return AK_InsufficientMemory;
    }

    m_uParamsMaxFrames = 0;
    return AK_Success;
}

AKRESULT SpatialClusteringFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    ReleaseMemory();
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT SpatialClusteringFX::Reset()
{
    if (m_bEngineInitialized)
        m_engine.Reset();

    if (m_pHashTableService != nullptr)
    {
        if (HasMemory(m_passthroughs))
            m_pHashTableService->Reset(&m_passthroughs);
        if (HasMemory(m_clusters))
            m_pHashTableService->Reset(&m_clusters);
        if (HasMemory(m_outputIndices))
            m_pHashTableService->Reset(&m_outputIndices);
    }

    m_requests.RemoveAll();
    m_priorityObjects.RemoveAll();
    m_priorityValues.RemoveAll();
    m_uNumOutputs = 0;
    m_uParamsMaxFrames = 0;
    return AK_Success;
}

AKRESULT SpatialClusteringFX::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
    out_rPluginInfo.eType = AkPluginTypeEffect;
    out_rPluginInfo.bIsInPlace = false;
    out_rPluginInfo.bCanProcessObjects = true;
    out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
    return AK_Success;
}

void SpatialClusteringFX::Execute(const AkAudioObjects& in_objects, const AkAudioObjects& out_objects)
{
    ++m_uFrame;

    const AkUInt32 uExistingOutputs = out_objects.uNumObjects;
    const AkUInt16 uMaxFrames = in_objects.uNumObjects > 0
        ? in_objects.ppObjectBuffers[0]->MaxFrames()
        : m_pContext->GlobalContext()->GetMaxBufferLength();
    UpdateClusterParams(uMaxFrames);

    if (!PrepareInputs(in_objects) || !m_engine.Update(m_inputs.Data(), in_objects.uNumObjects, m_clusterParams))
    {
        PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);
        KeepOutputsSilent(uExistingOutputs);
        return;
    }

    if (m_engine.IsOverBudget())
        PostRateLimitedMessage(MessageKind_OverBudget, kOverBudgetMessage, AK::Monitor::ErrorLevel_Message);

    const AkUInt32 uCreated = CreateOutputs(in_objects);

    if (!FetchOutputs(uExistingOutputs + uCreated))
    {
        PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);
        return;
    }

    if (!BuildOutputIndex())
    {
        PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);
        SilenceOutputs();
        return;
    }

    m_priorityObjects.RemoveAll();
    m_priorityValues.RemoveAll();

    ProcessPassthroughs(in_objects);
    ProcessClusters(in_objects);
    ReleaseUnwrittenOutputs();
    ApplyPriorities();
    RemoveStaleEntries();
}

void SpatialClusteringFX::ReleaseMemory()
{
    if (m_bEngineInitialized)
        m_engine.Term();
    m_bEngineInitialized = false;

    m_inputs.Term();
    m_inputPriorities.Term();
    m_requests.Term();
    m_newObjects.Term();
    m_outputObjects.Term();
    m_outputBuffers.Term();
    m_clusterOutputs.Term();
    m_clusterValidFrames.Term();
    m_clusterHasData.Term();
    m_outputWritten.Term();
    m_priorityObjects.Term();
    m_priorityValues.Term();

    if (m_pHashTableService != nullptr && m_pAllocator != nullptr)
    {
        if (HasMemory(m_passthroughs))
            m_pHashTableService->Term(&m_passthroughs, m_pAllocator);
        if (HasMemory(m_clusters))
            m_pHashTableService->Term(&m_clusters, m_pAllocator);
        if (HasMemory(m_outputIndices))
            m_pHashTableService->Term(&m_outputIndices, m_pAllocator);
    }

    m_passthroughs = AK::AkHashTable<AkUInt64, PassthroughEntry>();
    m_clusters = AK::AkHashTable<AkUInt64, ClusterEntry>();
    m_outputIndices = AK::AkHashTable<AkUInt64, AkUInt32>();
    m_uNumOutputs = 0;
}

void SpatialClusteringFX::UpdateClusterParams(AkUInt16 in_uMaxFrames)
{
    if (in_uMaxFrames == m_uParamsMaxFrames && !m_pParams->m_paramChangeHandler.HasAnyChanged())
        return;

    m_uParamsMaxFrames = in_uMaxFrames;

    const SpatialClusteringRTPCParams& rtpc = m_pParams->RTPC;
    const AkReal32 fFrameLength = static_cast<AkReal32>(in_uMaxFrames > 0 ? in_uMaxFrames : 1);
    const AkReal32 fSampleRate = static_cast<AkReal32>(m_uSampleRate > 0 ? m_uSampleRate : 48000);
    const AkReal32 fFrameSeconds = fFrameLength / fSampleRate;

    m_clusterParams.joinAngle = Clamp(rtpc.joinAngle, 0.f, PARAM_JOIN_ANGLE_MAX);
    m_clusterParams.leaveRatio = Clamp(rtpc.leaveRatio, PARAM_LEAVE_RATIO_MIN, PARAM_LEAVE_RATIO_MAX);
    m_clusterParams.dwellFrames = MillisecondsToFrames(Clamp(rtpc.dwellTime, 0.f, PARAM_DWELL_TIME_MAX), fFrameSeconds);
    const AkUInt32 uCrossfadeFrames = MillisecondsToFrames(Clamp(rtpc.crossfadeTime, 0.f, PARAM_CROSSFADE_TIME_MAX), fFrameSeconds);
    m_clusterParams.crossfadeFrames = uCrossfadeFrames > 0 ? uCrossfadeFrames : 1;
    m_clusterParams.maxOutputs = static_cast<uint32_t>(Clamp(rtpc.maxOutputs, 0, PARAM_MAX_OUTPUTS_MAX));
    m_clusterParams.nearFieldRadius = Clamp(rtpc.nearFieldRadius, 0.f, PARAM_NEAR_FIELD_RADIUS_MAX);
    m_clusterParams.distanceWeight = Clamp(rtpc.distanceWeight, 0.f, 1.f);
    m_clusterParams.excludePriorityAbove = static_cast<uint32_t>(Clamp(rtpc.excludePriorityAbove, 0, PARAM_PRIORITY_MAX));
    m_clusterParams.loudnessWeighting = m_pParams->NonRTPC.loudnessWeighting;
    m_clusterParams.loudnessSmoothing = std::exp(-fFrameSeconds / kLoudnessTimeConstant);
    m_fSpreadFromSize = Clamp(rtpc.spreadFromSize, 0.f, 1.f);

    const AkUInt32 uMessageInterval = static_cast<AkUInt32>(kMessageIntervalSeconds / fFrameSeconds);
    m_uMessageInterval = uMessageInterval > 0 ? uMessageInterval : 1;

    m_pParams->m_paramChangeHandler.ResetAllParamChanges();
}

bool SpatialClusteringFX::PrepareInputs(const AkAudioObjects& in_objects)
{
    const AkUInt32 uNumInputs = in_objects.uNumObjects;
    if (!m_inputs.Resize(uNumInputs) || !m_inputPriorities.Resize(uNumInputs))
        return false;

    if (uNumInputs > 0 && m_pPriorityService != nullptr)
    {
        m_pPriorityService->GetPriorities(in_objects.ppObjects, uNumInputs, m_inputPriorities.Data());
    }
    else
    {
        for (AkUInt32 i = 0; i < uNumInputs; ++i)
            m_inputPriorities[i] = AK_DEFAULT_PRIORITY;
    }

    const bool bLoudnessWeighting = m_clusterParams.loudnessWeighting;
    for (AkUInt32 i = 0; i < uNumInputs; ++i)
    {
        const AkAudioObject& object = *in_objects.ppObjects[i];
        AkAudioBuffer& buffer = *in_objects.ppObjectBuffers[i];
        const AkVector& position = object.positioning.threeD.xform.Position();

        ClusterInput& input = m_inputs[i];
        input.key = object.key;
        input.position = ClusterVec3{ position.X, position.Y, position.Z };
        input.clusterable = IsClusterable(object, buffer);
        input.loudness = (bLoudnessWeighting && input.clusterable) ? ComputeRms(buffer) : 0.f;
        input.priority = static_cast<uint32_t>(Clamp(static_cast<AkInt32>(m_inputPriorities[i]), 0, PARAM_PRIORITY_MAX));
        input.group = HashCustomMetadata(object);
    }

    return true;
}

AkUInt32 SpatialClusteringFX::CreateOutputs(const AkAudioObjects& in_objects)
{
    m_requests.RemoveAll();
    bool bOutOfMemory = false;

    for (AkUInt32 i = 0; i < in_objects.uNumObjects; ++i)
    {
        if (m_inputs[i].clusterable)
            continue;

        const AkUInt32 uChannelConfig = in_objects.ppObjectBuffers[i]->GetChannelConfig().Serialize();
        const AkInt32 iSlot = FindSlot(m_passthroughs, m_inputs[i].key);
        if (iSlot >= 0)
        {
            if (m_passthroughs.ValueAt(static_cast<AkUInt32>(iSlot)).channelConfig == uChannelConfig)
                continue;
            m_pHashTableService->RemoveSlot(&m_passthroughs, iSlot);
        }

        if (!AddRequest(uChannelConfig, i, false))
            bOutOfMemory = true;
    }

    AkChannelConfig monoConfig;
    monoConfig.SetStandard(AK_SPEAKER_SETUP_MONO);
    const AkUInt32 uMonoConfig = monoConfig.Serialize();

    const AkUInt32 uNumClusters = m_engine.GetClusterCount();
    for (AkUInt32 c = 0; c < uNumClusters; ++c)
    {
        const ClusterInfo& info = m_engine.GetCluster(c);
        const AkInt32 iSlot = FindSlot(m_clusters, static_cast<AkUInt64>(info.id));
        if (iSlot >= 0)
        {
            if (!info.isNew)
                continue;
            m_pHashTableService->RemoveSlot(&m_clusters, iSlot);
        }

        if (!AddRequest(uMonoConfig, c, true))
            bOutOfMemory = true;
    }

    if (bOutOfMemory)
        PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);

    AkUInt32 uCreated = 0;
    const AkUInt32 uNumRequests = m_requests.Length();
    for (AkUInt32 r = 0; r < uNumRequests; ++r)
    {
        if (m_requests[r].done)
            continue;

        const AkUInt32 uChannelConfig = m_requests[r].channelConfig;
        AkUInt32 uBatchSize = 0;
        for (AkUInt32 k = r; k < uNumRequests; ++k)
        {
            if (!m_requests[k].done && m_requests[k].channelConfig == uChannelConfig)
                ++uBatchSize;
        }

        bool bCreated = false;
        if (m_newObjects.Resize(uBatchSize))
        {
            AkChannelConfig channelConfig;
            channelConfig.Deserialize(uChannelConfig);
            AkAudioObjects newObjects(uBatchSize, nullptr, m_newObjects.Data());
            bCreated = m_pContext->CreateOutputObjects(channelConfig, newObjects) == AK_Success;
            if (!bCreated)
                PostRateLimitedMessage(MessageKind_CreateFailed, kCreateFailedMessage, AK::Monitor::ErrorLevel_Error);
        }
        else
        {
            PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);
        }

        AkUInt32 uObject = 0;
        for (AkUInt32 k = r; k < uNumRequests; ++k)
        {
            OutputRequest& request = m_requests[k];
            if (request.done || request.channelConfig != uChannelConfig)
                continue;

            request.done = true;
            if (bCreated)
                RegisterOutput(in_objects, request, m_newObjects[uObject++]);
        }

        if (bCreated)
            uCreated += uBatchSize;
    }

    return uCreated;
}

bool SpatialClusteringFX::AddRequest(AkUInt32 in_uChannelConfig, AkUInt32 in_uSource, bool in_bIsCluster)
{
    OutputRequest* pRequest = m_requests.AddLast();
    if (pRequest == nullptr)
        return false;

    pRequest->channelConfig = in_uChannelConfig;
    pRequest->source = in_uSource;
    pRequest->isCluster = in_bIsCluster;
    pRequest->done = false;
    return true;
}

void SpatialClusteringFX::RegisterOutput(const AkAudioObjects& in_objects, const OutputRequest& in_request, AkAudioObject* in_pObject)
{
    if (in_request.isCluster)
    {
        const ClusterInfo& info = m_engine.GetCluster(in_request.source);
        ClusterEntry* pEntry = static_cast<ClusterEntry*>(m_pHashTableService->AddKey(&m_clusters, m_pAllocator, static_cast<AkUInt64>(info.id)));
        if (pEntry == nullptr)
        {
            PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);
            return;
        }

        pEntry->outputKey = in_pObject->key;
        pEntry->lastFrame = m_uFrame - 1;

        char szName[32];
        FormatClusterName(info.id, szName);
        in_pObject->SetName(m_pAllocator, szName);
        return;
    }

    const AkAudioObject& input = *in_objects.ppObjects[in_request.source];
    PassthroughEntry* pEntry = static_cast<PassthroughEntry*>(m_pHashTableService->AddKey(&m_passthroughs, m_pAllocator, input.key));
    if (pEntry == nullptr)
    {
        PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);
        return;
    }

    pEntry->outputKey = in_pObject->key;
    pEntry->channelConfig = in_request.channelConfig;
    pEntry->lastFrame = m_uFrame - 1;

    const char* szInputName = input.objectName.Get();
    in_pObject->SetName(m_pAllocator, (szInputName != nullptr && szInputName[0] != '\0') ? szInputName : kPassthroughName);
}

bool SpatialClusteringFX::FetchOutputs(AkUInt32 in_uExpected)
{
    m_uNumOutputs = 0;

    AkUInt32 uCapacity = in_uExpected + 1;
    if (!m_outputObjects.Resize(uCapacity) || !m_outputBuffers.Resize(uCapacity))
        return false;

    AkAudioObjects outputs(uCapacity, m_outputBuffers.Data(), m_outputObjects.Data());
    m_pContext->GetOutputObjects(outputs);
    AkUInt32 uNumOutputs = outputs.uNumObjects;
    if (uNumOutputs >= uCapacity)
    {
        AkAudioObjects count;
        m_pContext->GetOutputObjects(count);
        uCapacity = count.uNumObjects;
        if (!m_outputObjects.Resize(uCapacity) || !m_outputBuffers.Resize(uCapacity))
            return false;

        AkAudioObjects allOutputs(uCapacity, m_outputBuffers.Data(), m_outputObjects.Data());
        m_pContext->GetOutputObjects(allOutputs);
        uNumOutputs = allOutputs.uNumObjects;
    }

    if (!m_outputWritten.Resize(uNumOutputs))
        return false;

    for (AkUInt32 i = 0; i < uNumOutputs; ++i)
        m_outputWritten[i] = 0;

    m_uNumOutputs = uNumOutputs;
    return true;
}

bool SpatialClusteringFX::BuildOutputIndex()
{
    m_pHashTableService->Reset(&m_outputIndices);

    for (AkUInt32 i = 0; i < m_uNumOutputs; ++i)
    {
        AkUInt32* pIndex = static_cast<AkUInt32*>(m_pHashTableService->AddKey(&m_outputIndices, m_pAllocator, m_outputObjects[i]->key));
        if (pIndex == nullptr)
            return false;
        *pIndex = i;
    }

    return true;
}

AkUInt32 SpatialClusteringFX::FindOutput(AkAudioObjectID in_key) const
{
    const AkInt32 iSlot = FindSlot(m_outputIndices, in_key);
    return iSlot >= 0 ? m_outputIndices.ValueAt(static_cast<AkUInt32>(iSlot)) : kInvalidIndex;
}

void SpatialClusteringFX::ProcessPassthroughs(const AkAudioObjects& in_objects)
{
    for (AkUInt32 i = 0; i < in_objects.uNumObjects; ++i)
    {
        if (m_inputs[i].clusterable)
            continue;

        const AkInt32 iSlot = FindSlot(m_passthroughs, m_inputs[i].key);
        if (iSlot < 0)
            continue;

        PassthroughEntry& entry = m_passthroughs.ValueAt(static_cast<AkUInt32>(iSlot));
        const AkUInt32 uOutput = FindOutput(entry.outputKey);
        if (uOutput == kInvalidIndex)
            continue;

        entry.lastFrame = m_uFrame;
        m_outputWritten[uOutput] = 1;

        AkAudioObject* pInObject = in_objects.ppObjects[i];
        AkAudioBuffer* pInBuffer = in_objects.ppObjectBuffers[i];
        AkAudioObject* pOutObject = m_outputObjects[uOutput];
        AkAudioBuffer* pOutBuffer = m_outputBuffers[uOutput];

        const AkUInt32 uNumChannels = AkMin(pInBuffer->NumChannels(), pOutBuffer->NumChannels());
        const AkUInt16 uFrames = AkMin(pInBuffer->uValidFrames, pOutBuffer->MaxFrames());
        for (AkUInt32 channel = 0; channel < uNumChannels; ++channel)
            AKPLATFORM::AkMemCpy(pOutBuffer->GetChannel(channel), pInBuffer->GetChannel(channel), static_cast<AkUInt32>(uFrames * sizeof(AkSampleType)));

        pOutBuffer->uValidFrames = uFrames;
        pOutBuffer->eState = pInBuffer->eState;
        pOutObject->positioning = pInObject->positioning;
        if (pOutObject->arCustomMetadata.Copy(pInObject->arCustomMetadata) != AK_Success)
            PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);

        QueuePriority(pOutObject, m_inputPriorities[i]);
    }
}

void SpatialClusteringFX::ProcessClusters(const AkAudioObjects& in_objects)
{
    const AkUInt32 uNumClusters = m_engine.GetClusterCount();
    if (!m_clusterOutputs.Resize(uNumClusters) || !m_clusterValidFrames.Resize(uNumClusters) || !m_clusterHasData.Resize(uNumClusters))
    {
        PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);
        return;
    }

    for (AkUInt32 c = 0; c < uNumClusters; ++c)
    {
        const ClusterInfo& info = m_engine.GetCluster(c);
        m_clusterOutputs[c] = kInvalidIndex;
        m_clusterValidFrames[c] = 0;
        m_clusterHasData[c] = 0;

        const AkInt32 iSlot = FindSlot(m_clusters, static_cast<AkUInt64>(info.id));
        if (iSlot < 0 || info.representative >= in_objects.uNumObjects)
            continue;

        ClusterEntry& entry = m_clusters.ValueAt(static_cast<AkUInt32>(iSlot));
        const AkUInt32 uOutput = FindOutput(entry.outputKey);
        if (uOutput == kInvalidIndex || m_outputBuffers[uOutput]->NumChannels() == 0)
            continue;

        entry.lastFrame = m_uFrame;
        m_clusterOutputs[c] = uOutput;

        AkAudioBuffer* pOutBuffer = m_outputBuffers[uOutput];
        const AkUInt32 uNumChannels = pOutBuffer->NumChannels();
        for (AkUInt32 channel = 0; channel < uNumChannels; ++channel)
            AKPLATFORM::AkMemSet(pOutBuffer->GetChannel(channel), 0, static_cast<AkUInt32>(pOutBuffer->MaxFrames() * sizeof(AkSampleType)));

        AkAudioObject* pOutObject = m_outputObjects[uOutput];
        const AkAudioObject* pRepresentative = in_objects.ppObjects[info.representative];
        pOutObject->positioning = pRepresentative->positioning;
        if (info.memberCount > 1)
        {
            Ak3dData& threeD = pOutObject->positioning.threeD;
            pOutObject->positioning.behavioral.spatMode = AK_SpatializationMode_PositionOnly;
            threeD.xform.SetPosition(info.position.x, info.position.y, info.position.z);
            threeD.xform.SetOrientation(
                AK_DEFAULT_LISTENER_FRONT_X, AK_DEFAULT_LISTENER_FRONT_Y, AK_DEFAULT_LISTENER_FRONT_Z,
                AK_DEFAULT_TOP_X, AK_DEFAULT_TOP_Y, AK_DEFAULT_TOP_Z);
            const AkReal32 fSizeSpread = m_fSpreadFromSize * Clamp(info.angularRadius / kFullSpreadRadius, 0.f, 1.f);
            threeD.spread = AkMax(pRepresentative->positioning.threeD.spread, fSizeSpread);
        }

        if (pOutObject->arCustomMetadata.Copy(pRepresentative->arCustomMetadata) != AK_Success)
            PostRateLimitedMessage(MessageKind_OutOfMemory, kOutOfMemoryMessage, AK::Monitor::ErrorLevel_Error);

        QueuePriority(pOutObject, static_cast<AkPriority>(Clamp<AkUInt32>(info.priority, 0, static_cast<AkUInt32>(PARAM_PRIORITY_MAX))));
    }

    const AkUInt32 uNumContributions = m_engine.GetContributionCount();
    for (AkUInt32 k = 0; k < uNumContributions; ++k)
    {
        const ClusterContribution& contribution = m_engine.GetContribution(k);
        if (contribution.cluster >= uNumClusters || contribution.input >= in_objects.uNumObjects)
            continue;

        const AkUInt32 uOutput = m_clusterOutputs[contribution.cluster];
        AkAudioBuffer* pInBuffer = in_objects.ppObjectBuffers[contribution.input];
        if (uOutput == kInvalidIndex || pInBuffer->NumChannels() == 0)
            continue;

        AkAudioBuffer* pOutBuffer = m_outputBuffers[uOutput];
        const AkUInt16 uFrames = AkMin(pInBuffer->uValidFrames, pOutBuffer->MaxFrames());
        const AkUInt32 uNumChannels = pInBuffer->NumChannels();
        const AkReal32 fDownmix = DownmixGain(uNumChannels);
        for (AkUInt32 channel = 0; channel < uNumChannels; ++channel)
            MixRamp(pInBuffer->GetChannel(channel), pOutBuffer->GetChannel(0), uFrames,
                contribution.gainStart * fDownmix, contribution.gainEnd * fDownmix);
        if (uFrames > m_clusterValidFrames[contribution.cluster])
            m_clusterValidFrames[contribution.cluster] = uFrames;
        if (pInBuffer->eState != AK_NoMoreData)
            m_clusterHasData[contribution.cluster] = 1;
    }

    for (AkUInt32 c = 0; c < uNumClusters; ++c)
    {
        const AkUInt32 uOutput = m_clusterOutputs[c];
        if (uOutput == kInvalidIndex)
            continue;

        AkAudioBuffer* pOutBuffer = m_outputBuffers[uOutput];
        pOutBuffer->uValidFrames = m_clusterValidFrames[c];
        pOutBuffer->eState = m_clusterHasData[c] != 0 ? AK_DataReady : AK_NoMoreData;
        m_outputWritten[uOutput] = 1;
    }
}

void SpatialClusteringFX::ReleaseUnwrittenOutputs()
{
    for (AkUInt32 i = 0; i < m_uNumOutputs; ++i)
    {
        if (m_outputWritten[i] != 0)
            continue;

        AkAudioBuffer* pBuffer = m_outputBuffers[i];
        const AkUInt32 uNumChannels = pBuffer->NumChannels();
        for (AkUInt32 channel = 0; channel < uNumChannels; ++channel)
            AKPLATFORM::AkMemSet(pBuffer->GetChannel(channel), 0, static_cast<AkUInt32>(pBuffer->MaxFrames() * sizeof(AkSampleType)));

        pBuffer->uValidFrames = 0;
        pBuffer->eState = AK_NoMoreData;
    }
}

void SpatialClusteringFX::QueuePriority(AkAudioObject* in_pObject, AkPriority in_priority)
{
    if (m_pPriorityService == nullptr)
        return;

    if (m_priorityObjects.AddLast(in_pObject) == nullptr)
        return;

    if (m_priorityValues.AddLast(in_priority) == nullptr)
        m_priorityObjects.RemoveLast();
}

void SpatialClusteringFX::ApplyPriorities()
{
    const AkUInt32 uCount = m_priorityObjects.Length();
    if (m_pPriorityService == nullptr || uCount == 0)
        return;

    m_pPriorityService->SetPriorities(m_priorityObjects.Data(), uCount, m_priorityValues.Data());
}

void SpatialClusteringFX::RemoveStaleEntries()
{
    const AkUInt32 uFrame = m_uFrame;

    m_pHashTableService->RemoveIf(
        static_cast<AK::AkHashTableBase<AkUInt64>*>(&m_passthroughs),
        [this, uFrame](AkUInt32 in_uSlot) { return m_passthroughs.ValueAt(in_uSlot).lastFrame != uFrame; });

    m_pHashTableService->RemoveIf(
        static_cast<AK::AkHashTableBase<AkUInt64>*>(&m_clusters),
        [this, uFrame](AkUInt32 in_uSlot) { return m_clusters.ValueAt(in_uSlot).lastFrame != uFrame; });
}

void SpatialClusteringFX::KeepOutputsSilent(AkUInt32 in_uExpected)
{
    if (FetchOutputs(in_uExpected))
        SilenceOutputs();
}

void SpatialClusteringFX::SilenceOutputs()
{
    for (AkUInt32 i = 0; i < m_uNumOutputs; ++i)
    {
        AkAudioBuffer* pBuffer = m_outputBuffers[i];
        const AkUInt32 uNumChannels = pBuffer->NumChannels();
        for (AkUInt32 channel = 0; channel < uNumChannels; ++channel)
            AKPLATFORM::AkMemSet(pBuffer->GetChannel(channel), 0, static_cast<AkUInt32>(pBuffer->MaxFrames() * sizeof(AkSampleType)));

        pBuffer->uValidFrames = pBuffer->MaxFrames();
        pBuffer->eState = AK_DataReady;
    }
}

void SpatialClusteringFX::PostRateLimitedMessage(MessageKind in_kind, const char* in_szMessage, AK::Monitor::ErrorLevel in_level)
{
    if (m_aMessagePosted[in_kind] && m_uFrame - m_aMessageFrames[in_kind] < m_uMessageInterval)
        return;

    m_aMessagePosted[in_kind] = true;
    m_aMessageFrames[in_kind] = m_uFrame;
    m_pContext->PostMonitorMessage(in_szMessage, in_level);
}
