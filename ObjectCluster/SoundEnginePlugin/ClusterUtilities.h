#pragma once


#pragma once

#include <map>
#include <vector>
#include "KMeans.h"
#include "ObjectClusterFXParams.h"
#include "CustomOperators.h"

/**
 * @class ClusterUtilities
 * @brief Utility class for generating and managing audio object clusters.
 */
class ClusterUtilities {
public:
    /// Type alias for a map of clusters.
    using ClusterMap = std::map<AkTransform, std::vector<AkAudioObjectID>>;

    /**
     * @brief Default constructor
     */
    ClusterUtilities();

    /**
     * @brief Default destructor
     */
    ~ClusterUtilities();

    /**
     * @brief Generates clusters using the K-means algorithm.
     * @param inObjects The input audio objects.
     * @return A map of clusters.
     */
    ClusterMap GenerateKmeansClusters(const AkAudioObjects& inObjects);

    /**
     * @brief Generates clusters based on positional data.
     * @param inObjects The input audio objects.
     * @return A map of clusters.
     */
    ClusterMap GeneratePositionalClusters(const AkAudioObjects& inObjects);

    /**
     * @brief Updates the parameters for the K-means algorithm.
     * @param params The new parameters.
     */
    void UpdateKMeansParams(const ObjectClusterFXParams* params);

    /**
     * @brief Finds an audio object by its key.
     * @param inObjects The input audio objects.
     * @param key The key of the audio object to find.
     * @return A pointer to the found audio object, or nullptr if not found.
     */
    AkAudioObject* FindAudioObjectByKey(const AkAudioObjects& inObjects, const AkAudioObjectID key);

    /**
     * @brief Finds an audio object buffer by its key.
     * @param inObjects The input audio objects.
     * @param key The key of the audio object buffer to find.
     * @return A pointer to the found audio object buffer, or nullptr if not found.
     */
    AkAudioBuffer* FindAudioObjectBufferByKey(const AkAudioObjects& inObjects, const AkAudioObjectID key);

private:
    KMeans m_kmeans; ///< Instance of the KMeans class for clustering.
};

