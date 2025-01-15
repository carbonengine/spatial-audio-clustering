/*
 * Copyright 2025 CCP ehf.
 *
 * This software was developed by CCP Games for spatial audio object clustering 
 * in EVE Online and EVE Frontier.
 * 
 * The content of this file includes portions of the AUDIOKINETIC Wwise Technology 
 * released in source code form as part of the SDK installer package.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This license does not grant any rights to CCP's trademarks or game content.
 * EVE Online and EVE Frontier are registered trademarks of CCP ehf.
 */

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