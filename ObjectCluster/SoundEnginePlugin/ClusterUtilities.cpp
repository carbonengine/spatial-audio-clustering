#include "ClusterUtilities.h"

ClusterUtilities::ClusterUtilities() = default;
ClusterUtilities::~ClusterUtilities() = default;

ClusterUtilities::ClusterMap ClusterUtilities::GenerateKmeansClusters(const AkAudioObjects& inObjects) {
    // Iterate through input objects and get ID and position
    std::vector<ObjectPosition> objectPositions;
    objectPositions.reserve(inObjects.uNumObjects);
    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inobj = inObjects.ppObjects[i];
        objectPositions.push_back({ inobj->positioning.threeD.xform.Position(), inobj->key });
    }

    // Check if objectPositions is empty
    if (objectPositions.empty()) {
        // Return an empty map if there are no objects to cluster
        return ClusterUtilities::ClusterMap();
    }

    // Perform clustering only if there are objects
    m_kmeans.performClustering(objectPositions);
    return m_kmeans.getClusters();
}

ClusterUtilities::ClusterMap ClusterUtilities::GeneratePositionalClusters(const AkAudioObjects& inObjects) {
    ClusterMap clusterMap;
    // Create new position clusters if needed and update existing ones.
    for (AkUInt32 i = 0; i < inObjects.uNumObjects; ++i) {
        AkAudioObject* inObject = inObjects.ppObjects[i];

        auto it = clusterMap.find(inObject->positioning.threeD.xform);
        if (it == clusterMap.end()) {
            std::vector<AkAudioObjectID> inObjectKeys;
            inObjectKeys.push_back(inObject->key);
            clusterMap.insert(std::make_pair(inObject->positioning.threeD.xform, inObjectKeys));
        }
        else {
            it->second.push_back(inObject->key);
        }
    }

    return clusterMap;
}

void ClusterUtilities::UpdateKMeansParams(const ObjectClusterFXParams* params) {
    m_kmeans.setDistanceThreshold(params->RTPC.distanceThreshold);
    m_kmeans.setTolerance(params->RTPC.tolerance);
}

AkAudioObject* ClusterUtilities::FindAudioObjectByKey(const AkAudioObjects& inObjects, const AkAudioObjectID key) {
    for (AkUInt32 j = 0; j < inObjects.uNumObjects; j++) {
        if (inObjects.ppObjects[j]->key == key) {
            return inObjects.ppObjects[j];
        }
    }
    return nullptr;
}

AkAudioBuffer* ClusterUtilities::FindAudioObjectBufferByKey(const AkAudioObjects& inObjects, const AkAudioObjectID key) {
    for (AkUInt32 j = 0; j < inObjects.uNumObjects; j++) {
        if (inObjects.ppObjects[j]->key == key) {
            return inObjects.ppObjectBuffers[j];
        }
    }
    return nullptr;
}