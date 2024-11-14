#pragma once

#include <AK/SoundEngine/Common/AkTypes.h>
#include "ObjectClusterFXParams.h"
#include <vector>




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

    /**
      * @brief Calculates the squared distance between two 3D vectors
      * @param v1 First vector
      * @param v2 Second vector
      * @return The squared Euclidean distance between the vectors
      */
    float GetDistanceSquared(const AkVector& v1, const AkVector& v2);


    /**
     * @brief Calculates the average position of a group of audio objects in a cluster.
     *
     * @param clusterObjects Vector of audio object IDs in the cluster
     * @param inObjects Input audio objects containing position data
     * @return AkVector The mean position of all objects in the cluster
     */ 
    AkVector CalculateMeanPosition(
        const std::vector<AkAudioObjectID>& clusterObjects,
        const AkAudioObjects& inObjects); 
};