#include "KMeans.h"

template <typename T>
T clamp(T value, T min, T max) {
    return std::max(min, std::min(max, value));
}
unsigned int KMeans::determineMaxClusters(unsigned int numObjects) {
    return static_cast<unsigned int>(std::sqrt(numObjects));
}

void KMeans::initializeCentroids(const std::vector<ObjectPosition>& objects) {
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(0, objects.size() - 1);

    centroids.clear();
    for (unsigned int i = 0; i < maxClusters; ++i) {
        bool validCentroid = false;
        int attempts = 0;
        while (!validCentroid && attempts < 100) {
            int index = distribution(generator);
            const AkVector& candidate = objects[index].position;

            validCentroid = true;
            for (const AkVector& existingCentroid : centroids) {
                if (calculateDistance(candidate, existingCentroid) < distanceThreshold) {
                    validCentroid = false;
                    break;
                }
            }

            if (validCentroid) {
                centroids.push_back(candidate);
            }

            attempts++;
        }

        if (!validCentroid) {
            break;  // Stop if we can't find a valid centroid
        }
    }
}

float KMeans::calculateDistance(const AkVector& a, const AkVector& b) const {
    return std::sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z));
}

bool KMeans::assignPointsToClusters(const std::vector<ObjectPosition>& objects) {
    bool changed = false;
    std::vector<std::vector<ObjectPosition>> newClusters(centroids.size());
    unassignedPoints.clear();  // Clear previous unassigned points

    for (size_t i = 0; i < objects.size(); ++i) {
        float minDistance = std::numeric_limits<float>::max();
        int closestCentroid = -1;
        for (size_t j = 0; j < centroids.size(); ++j) {
            float distance = calculateDistance(objects[i].position, centroids[j]);
            if (distance < minDistance) {
                minDistance = distance;
                closestCentroid = j;
            }
        }

        if (minDistance <= distanceThreshold) {
            // The point is within the distance threshold of a centroid
            if (labels[i] != closestCentroid) {
                labels[i] = closestCentroid;
                changed = true;
            }
            newClusters[closestCentroid].push_back(objects[i]);
        }
        else {
            // The point is too far from all centroids
            labels[i] = -1;
            unassignedPoints.push_back(objects[i]);
            changed = true;
        }
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
            if (calculateDistance(unassignedIt->position, newCentroid) <= distanceThreshold) {
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

bool KMeans::updateCentroids() {
    if (clusters.empty()) return false;

    bool changed = false;
    for (size_t i = 0; i < clusters.size(); ++i) {
        if (clusters[i].empty()) continue;

        AkVector new_centroid = { 0, 0, 0 };
        for (const auto& obj : clusters[i]) {
            new_centroid.X += obj.position.X;
            new_centroid.Y += obj.position.Y;
            new_centroid.Z += obj.position.Z;
        }
        new_centroid.X /= clusters[i].size();
        new_centroid.Y /= clusters[i].size();
        new_centroid.Z /= clusters[i].size();

        float distance = calculateDistance(centroids[i], new_centroid);
        if (distance > tolerance) {
            centroids[i] = new_centroid;
            changed = true;
        }
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

KMeans::KMeans(float tolerance, float distanceThreshold)
    : tolerance(tolerance), distanceThreshold(distanceThreshold) {
    std::random_device rd;
    seed = rd();
}

void KMeans::setTolerance(float newValue) {
    tolerance = clamp(newValue, 0.001f, 1.0f);
}

void KMeans::setDistanceThreshold(float newValue) {
    distanceThreshold = clamp(newValue, 0.1f, 1000.0f);
}

void KMeans::performClustering(const std::vector<ObjectPosition>& objects, unsigned int max_iterations) {
    labels.resize(objects.size(), -1);
    maxClusters = determineMaxClusters(objects.size());

    // Initialize centroids
    initializeCentroids(objects);

    for (unsigned int iter = 0; iter < max_iterations; ++iter) {
        // Assign points to clusters
        bool changed = assignPointsToClusters(objects);

        // Adjust cluster count based on unassigned points
        adjustClusterCount();

        // Update centroids
        bool centroidsUpdated = updateCentroids();

        // Calculate SSE
        float current_sse = calculateSSE();
        sse_values.push_back(current_sse);

        // Check for convergence
        if ((!changed && !centroidsUpdated) ||
            (iter > 0 && std::abs(sse_values[iter] - sse_values[iter - 1]) < tolerance * sse_values[iter - 1])) {
            break;
        }
    }

    // Final adjustment of cluster count
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