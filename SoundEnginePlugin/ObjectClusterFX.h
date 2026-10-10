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

#ifndef ObjectClusterFX_H
#define ObjectClusterFX_H

#include "ClusterEngine.h"
#include "ObjectClusterFXParams.h"

#include <AK/SoundEngine/Common/AkAudioObject.h>
#include <AK/SoundEngine/Common/IAkPluginHashTable.h>
#include <AK/Tools/Common/AkArray.h>
#include <AK/Tools/Common/AkMonitorError.h>

/** @brief Cluster engine allocator that allocates through the Wwise plug-in allocator. */
class ObjectClusterAllocator final : public ClusterAllocator
{
public:
    /** @brief Sets the Wwise plug-in allocator to allocate through. */
    void Init(AK::IAkPluginMemAlloc* in_pAllocator);

    /** @brief Allocates a block of memory, or returns nullptr. */
    void* Alloc(size_t in_size) override;

    /** @brief Frees a block returned by Alloc. */
    void Free(void* in_pBlock) override;

private:
    AK::IAkPluginMemAlloc* m_pAllocator = nullptr; ///< Wwise plug-in allocator.
};

/**
 * @class ObjectClusterFX
 * @brief Audio object clustering effect plugin
 * @details Implements audio object clustering and processing functionality as an out-of-place plugin
 */
class ObjectClusterFX
    : public AK::IAkOutOfPlaceObjectPlugin
{
public:
    /** @brief Creates the effect. */
    ObjectClusterFX();

    /** @brief Destroys the effect. */
    ~ObjectClusterFX();

    /**
     * @brief Initializes the plugin
     * @param in_pAllocator Memory allocator interface
     * @param in_pContext Plugin context interface
     * @param in_pParams Plugin parameters
     * @param io_rFormat Audio format
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& io_rFormat) override;

    /**
     * @brief Terminates the plugin and releases resources
     * @param in_pAllocator Memory allocator interface
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT Term(AK::IAkPluginMemAlloc* in_pAllocator) override;

    /**
     * @brief Resets the plugin to its initial state
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT Reset() override;

    /**
     * @brief Retrieves plugin information
     * @param out_rPluginInfo Structure to be filled with plugin information
     * @return AKRESULT Status code indicating success or failure
     */
    AKRESULT GetPluginInfo(AkPluginInfo& out_rPluginInfo) override;

    /**
     * @brief Executes the audio processing
     * @param in_objects Input audio objects and buffers
     * @param out_objects Output audio objects and buffers
     */
    void Execute(const AkAudioObjects& in_objects, const AkAudioObjects& out_objects) override;

private:
    /** @brief Array type that allocates through the Wwise plug-in allocator. */
    template <typename T>
    using PluginArray = AkArray<T, const T&, AkPluginArrayAllocator>;

    /** @brief Output object that carries one passthrough input. */
    struct PassthroughEntry
    {
        AkAudioObjectID outputKey; ///< Key of the output object.
        AkUInt32 channelConfig;    ///< Serialized channel configuration of the output object.
        AkUInt32 lastFrame;        ///< Frame in which the output object was last written.
    };

    /** @brief Output object that carries one cluster. */
    struct ClusterEntry
    {
        AkAudioObjectID outputKey; ///< Key of the output object.
        AkUInt32 lastFrame;        ///< Frame in which the output object was last written.
    };

    /** @brief Output object to create in the current frame. */
    struct OutputRequest
    {
        AkUInt32 channelConfig; ///< Serialized channel configuration of the new output object.
        AkUInt32 source;        ///< Input index for a passthrough output, or cluster index for a cluster output.
        bool isCluster;         ///< True if the output object carries a cluster.
        bool done;              ///< True once the request has been handled.
    };

    /** @brief Kinds of monitor messages, each rate-limited on its own. */
    enum MessageKind : AkUInt32
    {
        MessageKind_CreateFailed, ///< Output objects could not be created.
        MessageKind_OutOfMemory,  ///< Memory ran out.
        MessageKind_OverBudget,   ///< More outputs were needed than the maximum allows.
        MessageKind_Count         ///< Number of message kinds.
    };

    /** @brief Frees all memory and leaves the effect ready for another Init. */
    void ReleaseMemory();

    /** @brief Recomputes the cluster engine settings when parameters or the buffer length change. */
    void UpdateClusterParams(AkUInt16 in_uMaxFrames);

    /** @brief Fills the cluster engine inputs for this frame and returns false if memory ran out. */
    bool PrepareInputs(const AkAudioObjects& in_objects);

    /** @brief Creates output objects for new passthrough inputs and new clusters and returns how many were created. */
    AkUInt32 CreateOutputs(const AkAudioObjects& in_objects);

    /** @brief Adds an output object to create in this frame and returns false if memory ran out. */
    bool AddRequest(AkUInt32 in_uChannelConfig, AkUInt32 in_uSource, bool in_bIsCluster);

    /** @brief Records and names one new output object. */
    void RegisterOutput(const AkAudioObjects& in_objects, const OutputRequest& in_request, AkAudioObject* in_pObject);

    /** @brief Fetches all output objects into the output arrays and returns false if memory ran out. */
    bool FetchOutputs(AkUInt32 in_uExpected);

    /** @brief Builds the lookup from output key to output index and returns false if memory ran out. */
    bool BuildOutputIndex();

    /** @brief Returns the index of an output object in the output arrays, or kInvalidIndex. */
    AkUInt32 FindOutput(AkAudioObjectID in_key) const;

    /** @brief Copies each passthrough input to its output object. */
    void ProcessPassthroughs(const AkAudioObjects& in_objects);

    /** @brief Mixes the clusters into their output objects and sets their metadata. */
    void ProcessClusters(const AkAudioObjects& in_objects);

    /** @brief Writes an empty last frame to every fetched output object that was not written in this frame, so that the sound engine destroys it. */
    void ReleaseUnwrittenOutputs();

    /** @brief Queues a priority to set on an output object. */
    void QueuePriority(AkAudioObject* in_pObject, AkPriority in_priority);

    /** @brief Sets the queued priorities on the output objects. */
    void ApplyPriorities();

    /** @brief Forgets output objects that were not written in this frame. */
    void RemoveStaleEntries();

    /** @brief Fetches all output objects and keeps them alive with silence for this frame. */
    void KeepOutputsSilent(AkUInt32 in_uExpected);

    /** @brief Writes silence to the fetched output objects and keeps them alive for this frame. */
    void SilenceOutputs();

    /** @brief Posts a monitor message unless one of the same kind was posted recently. */
    void PostRateLimitedMessage(MessageKind in_kind, const char* in_szMessage, AK::Monitor::ErrorLevel in_level);

    /** @brief Index value that marks a missing output object. */
    static constexpr AkUInt32 kInvalidIndex = 0xFFFFFFFF;

    ObjectClusterFXParams* m_pParams;                            ///< Parameter node.
    AK::IAkPluginMemAlloc* m_pAllocator;                         ///< Wwise plug-in allocator.
    AK::IAkEffectPluginContext* m_pContext;                      ///< Effect context.
    AK::IAkPluginServiceAudioObjectPriority* m_pPriorityService; ///< Service that reads and writes object priorities.
    AK::IAkPluginServiceHashTable* m_pHashTableService;          ///< Service that manages the hash tables.

    ObjectClusterAllocator m_engineAllocator; ///< Allocator the cluster engine allocates through.
    ClusterEngine m_engine;                   ///< Cluster engine.
    bool m_bEngineInitialized;                ///< True once Init was called on the cluster engine.
    ClusterParams m_clusterParams;            ///< Cluster engine settings for the current parameters.
    AkReal32 m_fSpreadFromSize;               ///< Share of the cluster's angular size used as its minimum spread.
    AkUInt32 m_uSampleRate;                   ///< Sample rate in Hz.
    AkUInt16 m_uParamsMaxFrames;              ///< Buffer length the cluster engine settings were computed for.
    AkUInt32 m_uMessageInterval;              ///< Minimum number of frames between two messages of the same kind.
    AkUInt32 m_uFrame;                        ///< Number of the current frame.
    AkUInt32 m_uNumOutputs;                   ///< Number of output objects fetched in this frame.

    AkUInt32 m_aMessageFrames[MessageKind_Count]; ///< Frame in which each kind of message was last posted.
    bool m_aMessagePosted[MessageKind_Count];     ///< True for each kind of message that was posted at least once.

    PluginArray<ClusterInput> m_inputs;              ///< Cluster engine inputs for this frame.
    PluginArray<AkPriority> m_inputPriorities;       ///< Priority of each input for this frame.
    PluginArray<OutputRequest> m_requests;           ///< Output objects to create in this frame.
    PluginArray<AkAudioObject*> m_newObjects;        ///< Objects returned by one call to CreateOutputObjects.
    PluginArray<AkAudioObject*> m_outputObjects;     ///< Output objects for this frame.
    PluginArray<AkAudioBuffer*> m_outputBuffers;     ///< Output buffers for this frame.
    PluginArray<AkUInt32> m_clusterOutputs;          ///< Output index of each reported cluster for this frame.
    PluginArray<AkUInt16> m_clusterValidFrames;      ///< Valid frames of each reported cluster for this frame.
    PluginArray<AkUInt8> m_clusterHasData;           ///< Nonzero for each reported cluster with an input that has not ended in this frame.
    PluginArray<AkUInt8> m_outputWritten;            ///< Nonzero for each fetched output object written in this frame.
    PluginArray<AkAudioObject*> m_priorityObjects;   ///< Output objects whose priority is set in this frame.
    PluginArray<AkPriority> m_priorityValues;        ///< Priorities to set on the output objects.

    AK::AkHashTable<AkUInt64, PassthroughEntry> m_passthroughs; ///< Passthrough outputs by input key.
    AK::AkHashTable<AkUInt64, ClusterEntry> m_clusters;         ///< Cluster outputs by cluster ID.
    AK::AkHashTable<AkUInt64, AkUInt32> m_outputIndices;        ///< Output index by output key for this frame.
};

#endif // ObjectClusterFX_H
