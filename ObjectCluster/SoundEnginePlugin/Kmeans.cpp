/*
 * Copyright 2024 CCP ehf.
 *
 * This software was developed by CCP Games for spatial audio object clustering 
 * in EVE Online and EVE Frontier.
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

#include "KMeans.h"

template <typename T>
T clamp(T value, T min, T max) {
    return std::max(min, std::min(max, value));
}
unsigned int KMeans::determineMaxClusters(unsigned int numObjects) {
    return static_cast<unsigned int>(std::sqrt(numObjects));
}

void KMeans::initializeCentroids(const std::vector<ObjectPosition>& objects) {
    if (objects.empty()) return;

    std::vector<ObjectMetadata> objectsMetadata;
    objectsMetadata.reserve(objects.size());

    // Use a smaller radius for density calculation when threshold is small
    const float densityRadius = m_distanceThreshold * 0.25f; // More sensitive to local density
    const float densityRadiusSq = densityRadius * densityRadius;

    // Track density around origin specifically
    float originDensity = 0.0f;
    std::vector<const ObjectPosition*> nearOriginObjects;

    for (const auto& obj : objects) {
        float localDensity = 0.0f;

        // Calculate density contribution to origin
        float distToOriginSq = m_utilities.GetDistanceSquared(obj.position, AkVector{ 0,0,0 });
        if (distToOriginSq < densityRadiusSq) {
            nearOriginObjects.push_back(&obj);
            originDensity += calculateGaussianWeight(distToOriginSq, densityRadiusSq);
        }

        // Calculate local density relative to other points
        for (const auto& neighbor : objects) {
            float distSq = m_utilities.GetDistanceSquared(obj.position, neighbor.position);
            if (distSq < densityRadiusSq) {
                localDensity += calculateGaussianWeight(distSq, densityRadiusSq);
            }
        }

        objectsMetadata.push_back({ obj, localDensity, std::numeric_limits<float>::max() });
    }

    // Sort by density
    std::sort(objectsMetadata.begin(), objectsMetadata.end(),
        [](const ObjectMetadata& a, const ObjectMetadata& b) {
            return a.localDensity > b.localDensity;
        });

    centroids.clear();

    // If we have significant density near origin, calculate optimal centroid position
    if (!nearOriginObjects.empty()) {
        AkVector originCluster{ 0,0,0 };
        float totalWeight = 0.0f;

        // Calculate weighted average position for objects near origin
        for (const auto* obj : nearOriginObjects) {
            float weight = calculateGaussianWeight(
                m_utilities.GetDistanceSquared(obj->position, AkVector{ 0,0,0 }),
                densityRadiusSq
            );
            originCluster.X += obj->position.X * weight;
            originCluster.Y += obj->position.Y * weight;
            originCluster.Z += obj->position.Z * weight;
            totalWeight += weight;
        }

        if (totalWeight > 0.0f) {
            originCluster.X /= totalWeight;
            originCluster.Y /= totalWeight;
            originCluster.Z /= totalWeight;
            centroids.push_back(originCluster);
        }
    }

    // Add highest density point if we haven't added an origin cluster
    if (centroids.empty() && !objectsMetadata.empty()) {
        centroids.push_back(objectsMetadata[0].object.position);
    }

    // Continue with k-means++ initialization for remaining centroids
    while (centroids.size() < maxClusters) {
        float maxMinDistance = 0.0f;
        size_t bestCandidate = 0;
        bool candidateFound = false;

        for (size_t i = 0; i < objectsMetadata.size(); ++i) {
            float minDist = std::numeric_limits<float>::max();
            for (const auto& centroid : centroids) {
                float dist = calculateDistance(objectsMetadata[i].object.position, centroid);
                minDist = std::min(minDist, dist);
            }

            // More likely to create new centroids with smaller thresholds
            if (minDist > maxMinDistance && minDist > m_distanceThreshold * 0.5f) {
                maxMinDistance = minDist;
                bestCandidate = i;
                candidateFound = true;
            }
        }

        if (!candidateFound) break;
        centroids.push_back(objectsMetadata[bestCandidate].object.position);
    }
}

float KMeans::calculateDistance(const AkVector& a, const AkVector& b) const {
    return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));
}

bool KMeans::assignPointsToClusters(const std::vector<ObjectPosition>& objects) {
    if (objects.empty()) return false;

    bool changed = false;
    std::vector<std::vector<ObjectPosition>> newClusters(centroids.size());
    unassignedPoints.clear();

    // Assign points to nearest centroid if within threshold
    for (const auto& obj : objects) {
        float minDistance = std::numeric_limits<float>::max();
        int closestCentroid = -1;

        // Find closest centroid
        for (size_t j = 0; j < centroids.size(); ++j) {
            float distance = calculateDistance(obj.position, centroids[j]);
            if (distance < minDistance) {
                minDistance = distance;
                closestCentroid = j;
            }
        }

        // If closest centroid is within threshold, assign to cluster
        if (closestCentroid >= 0 && minDistance <= m_distanceThreshold) {
            newClusters[closestCentroid].push_back(obj);
            if (labels[&obj - &objects[0]] != closestCentroid) {
                labels[&obj - &objects[0]] = closestCentroid;
                changed = true;
            }
        }
        else {
            // Point is too far from any existing cluster
            unassignedPoints.push_back(obj);
            labels[&obj - &objects[0]] = -1;
            changed = true;
        }
    }

    // Remove empty clusters
    auto it = std::remove_if(newClusters.begin(), newClusters.end(),
        [](const std::vector<ObjectPosition>& cluster) { return cluster.empty(); });
    newClusters.erase(it, newClusters.end());

    // Form new clusters from unassigned points if they're close to each other
    while (!unassignedPoints.empty()) {
        std::vector<ObjectPosition> newCluster;
        newCluster.push_back(unassignedPoints[0]);
        AkVector clusterCenter = unassignedPoints[0].position;

        // Find all points within m_distanceThreshold of the first point
        // No increment in loop control: when we remove a point, next point slides into current position
        for (size_t i = 1; i < unassignedPoints.size();) {
            if (calculateDistance(unassignedPoints[i].position, clusterCenter) <= m_distanceThreshold) {
                newCluster.push_back(unassignedPoints[i]);
                unassignedPoints.erase(unassignedPoints.begin() + i);
            }
            else {
                ++i;
            }
        }
        unassignedPoints.erase(unassignedPoints.begin());

        if (newCluster.size() > 1) {
            newClusters.push_back(std::move(newCluster));
            changed = true;
        }
    }

    // Update centroids
    centroids.clear();
    for (const auto& cluster : newClusters) {
        centroids.push_back(calculateCentroid(cluster));
    }

    clusters = std::move(newClusters);
    return changed;
}

void KMeans::adjustClusterCount() {
    // Remove empty clusters
    auto it = std::remove_if(clusters.begin(), clusters.end(),
        [](const std::vector<ObjectPosition>& cluster) { return cluster.empty(); });
    clusters.erase(it, clusters.end());

    // Remove corresponding centroids
    centroids.resize(clusters.size());

    // Try to create new clusters from unassigned points
    while (!unassignedPoints.empty() && clusters.size() < maxClusters) {
        AkVector newCentroid = unassignedPoints[0].position;
        std::vector<ObjectPosition> newCluster;

        auto unassignedIt = unassignedPoints.begin();
        while (unassignedIt != unassignedPoints.end()) {
            if (calculateDistance(unassignedIt->position, newCentroid) <= m_distanceThreshold) {
                newCluster.push_back(*unassignedIt);
                unassignedIt = unassignedPoints.erase(unassignedIt);
            }
            else {
                ++unassignedIt;
            }
        }

        if (!newCluster.empty()) {
            clusters.push_back(std::move(newCluster));
            centroids.push_back(newCentroid);
        }
        else {
            break;  // No more clusters can be formed
        }
    }
}

float KMeans::calculateGaussianWeight(float distanceSquared, float radiusSquared) const
{
    return std::exp(-distanceSquared / (2.0f * radiusSquared));
}

void KMeans::setMinDistanceThreshold(float newValue)
{
    if (newValue != m_minThreshold) {
        m_minThreshold = newValue;
    }
}

void KMeans::setMaxDistanceThreshold(float newValue)
{
    if (newValue != m_maxThreshold) {
        m_maxThreshold = newValue;
    }
}

bool KMeans::updateCentroids() {
    if (clusters.empty()) return false;

    bool changed = false;
    std::vector<AkVector> newCentroids;
    newCentroids.reserve(clusters.size());

    for (const auto& cluster : clusters) {
        if (cluster.empty()) continue;

        AkVector newCentroid = calculateCentroid(cluster);
        newCentroids.push_back(newCentroid);
    }

    // Check if any centroids moved significantly
    for (size_t i = 0; i < newCentroids.size(); ++i) {
        if (calculateDistance(centroids[i], newCentroids[i]) > m_tolerance) {
            changed = true;
            break;
        }
    }

    if (changed) {
        centroids = std::move(newCentroids);
    }

    return changed;
}

void KMeans::adjustClusterCount(unsigned int numObjects) {
    if (numObjects == 0) {
        return;
    }
    maxClusters = determineMaxClusters(numObjects);
    centroids.resize(maxClusters);
}

float KMeans::calculateSSE() const {
    float sse = 0.0f;
    for (size_t i = 0; i < clusters.size(); ++i) {
        for (const auto& obj : clusters[i]) {
            float distance = calculateDistance(obj.position, centroids[i]);
            sse += distance * distance;  // Still square here as SSE is defined with squared distances
        }
    }
    return sse;
}

AkVector KMeans::calculateCentroid(const std::vector<ObjectPosition>& cluster)
{
    AkVector centroid = { 0, 0, 0 };
    if (cluster.empty()) return centroid;

    for (const auto& obj : cluster) {
        centroid.X += obj.position.X;
        centroid.Y += obj.position.Y;
        centroid.Z += obj.position.Z;
    }
    centroid.X /= cluster.size();
    centroid.Y /= cluster.size();
    centroid.Z /= cluster.size();

    return centroid;
}

KMeans::KMeans(float tolerance, float distanceThreshold, float minDistanceThreshold, float maxDistanceThreshold)
    : m_tolerance(tolerance),
    m_distanceThreshold(distanceThreshold),
    m_minThreshold(minDistanceThreshold),
    m_maxThreshold(maxDistanceThreshold)
{
    std::random_device rd;
    seed = rd();
}

void KMeans::setTolerance(float newValue) {
    m_tolerance = clamp(newValue, 0.001f, 1.0f);
}

void KMeans::setDistanceThreshold(float newValue) {

    if (newValue != m_distanceThreshold) {
        m_distanceThreshold = clamp(newValue, m_minThreshold, m_maxThreshold);

        // Clear existing state to force recalculation
        centroids.clear();
        labels.clear();
        clusters.clear();
        sse_values.clear();
        unassignedPoints.clear();
    }
}

void KMeans::performClustering(const std::vector<ObjectPosition>& objects, unsigned int max_iterations) {
    // Resize labels vector to match input size
    labels.resize(objects.size(), -1);

    // Recalculate max clusters based on current objects
    maxClusters = determineMaxClusters(objects.size());

    // Always reinitialize centroids
    initializeCentroids(objects);

    for (unsigned int iter = 0; iter < max_iterations; ++iter) {
        bool changed = assignPointsToClusters(objects);
        adjustClusterCount();
        bool centroidsUpdated = updateCentroids();

        float current_sse = calculateSSE();
        sse_values.push_back(current_sse);

        // Check for convergence
        if ((!changed && !centroidsUpdated) ||
            (iter > 0 && std::abs(sse_values[iter] - sse_values[iter - 1]) < m_tolerance * sse_values[iter - 1])) {
            break;
        }
    }

    adjustClusterCount();
}

const std::vector<int>& KMeans::getLabels() const {
    return labels;
}

const std::vector<AkVector>& KMeans::getCentroids() const {
    return centroids;
}

std::map<AkVector, std::vector<AkAudioObjectID>> KMeans::getClusters() const {
    std::map<AkVector, std::vector<AkAudioObjectID>> clusterMap;

    for (const auto& cluster : clusters) {
        if (!cluster.empty()) {
            AkVector centroid = { 0, 0, 0 };
            for (const auto& obj : cluster) {
                centroid.X += obj.position.X;
                centroid.Y += obj.position.Y;
                centroid.Z += obj.position.Z;
            }
            centroid.X /= cluster.size();
            centroid.Y /= cluster.size();
            centroid.Z /= cluster.size();

            std::vector<AkAudioObjectID> objectIDs;
            for (const auto& obj : cluster) {
                objectIDs.push_back(obj.key);
            }

            clusterMap[centroid] = std::move(objectIDs);
        }
    }

    return clusterMap;
}