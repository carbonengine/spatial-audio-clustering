#pragma once

#include <AK/SoundEngine/Common/AkTypes.h>
#include "ObjectClusterFXParams.h"
#include "SharedStructures.h"




/**
 * @class DSPUtilities
 * @brief Utility class for various DSP (Digital Signal Processing) operations.
 */
class DSPUtilities {
public:

	/**
	 * @brief Default constructor
	 */
	DSPUtilities();

	/**
	 * @brief Default destructor
	 */
	~DSPUtilities();

    /**
	 * @brief Retrieves the buffers from an AkAudioObjects instance and clears them.
     * @param audioObjects The audio objects whose buffers need to be cleared.
     */
    void ClearBuffers(AkAudioObjects& outputObjects);

    /**
     * @brief Copies the contents of one audio buffer to another.
     * @param inBuffer The input buffer.
     * @param outBuffer The output buffer.
     */
    void CopyBuffer(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer);

    /**
     * @brief Creates an output audio object.
     * @param inobj The input audio object.
     * @param inObjects The collection of input audio objects.
     * @param index The index of the input object in the collection.
     * @param m_pContext The plugin context.
     * @return The ID of the created output audio object.
     */
    AkAudioObjectID CreateOutputObject(const AkAudioObject* inobj, const AkAudioObjects& inObjects, const AkUInt32 index, AK::IAkEffectPluginContext* m_pContext);

    /**
     * @brief Allocates memory for volume matrices.
     * @param volumeMatrix The volume matrix pointer to be allocated.
     * @param in_uNumChannelsIn The number of input channels.
     * @param in_uNumChannelsOut The number of output channels.
     * @return AK_Success if allocation is successful, otherwise AK_InsufficientMemory.
     */
    AKRESULT AllocateVolumes(AK::SpeakerVolumes::MatrixPtr& volumeMatrix, const AkUInt32 in_uNumChannelsIn, const AkUInt32 in_uNumChannelsOut);

    /**
     * @brief Applies a Wwise mix to the audio buffers.
     * @param inBuffer The input buffer.
     * @param outBuffer The output buffer.
     * @param cumulativeGain The cumulative gain ramp.
     * @param currentVolumes The current volume matrix.
     * @param prevVolumes The previous volume matrix.
     * @param m_pContext The plugin context.
     */
    void ApplyWwiseMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, const AK::SpeakerVolumes::MatrixPtr& currentVolumes, GeneratedObjects* pGeneratedObject, AK::IAkEffectPluginContext* m_pContext);

    /**
     * @brief Applies a custom mix to the audio buffers.
     * @param inBuffer The input buffer.
     * @param outBuffer The output buffer.
     * @param cumulativeGain The cumulative gain ramp.
     * @param currentVolumes The current volume matrix.
     */
    void ApplyCustomMix(AkAudioBuffer* inBuffer, AkAudioBuffer* outBuffer, const AkRamp& cumulativeGain, const AK::SpeakerVolumes::MatrixPtr& currentVolumes);

    /**
     * @brief Normalizes the audio buffer.
     * @param pBuffer The buffer to be normalized.
     */
    void NormalizeBuffer(AkAudioBuffer* pBuffer);
};
