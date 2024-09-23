#pragma once

#include <AK/SoundEngine/Common/AkTypes.h>
#include "ObjectClusterFXParams.h"




/**
 * @class DSPUtilities
 * @brief Utility class for various DSP (Digital Signal Processing) operations.
 */
class Utilities {
public:

	/**
	 * @brief Default constructor
	 */
	Utilities();

	/**
	 * @brief Default destructor
	 */
	~Utilities();

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
    AkAudioObjectID CreateOutputObject(const AkAudioObject* inobj, const AkAudioObjects& inObjects, const AkUInt32 index, AK::IAkEffectPluginContext* m_pContext, const AkVector* clusterPosition);
};
