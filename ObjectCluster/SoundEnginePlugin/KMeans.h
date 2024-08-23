#pragma once
#include <vector>
#include <random>
#include <limits>
#include <cmath>
#include <algorithm>
#include <AK/SoundEngine/Common/AkTypes.h>
#include <AK/SoundEngine/Common/AkCommonDefs.h>
#include <AK/Plugin/PluginServices/AkMixerInputMap.h>

#undef min
#undef max

template <typename T>
T clamp(T value, T min, T max) {
    return std::max(min, std::min(max, value));
}

struct ObjectPosition {
    AkVector position;
    AkAudioObjectID key;

    bool operator<(const ObjectPosition& other) const {
        if (position.X != other.position.X) return position.X < other.position.X;
        if (position.Y != other.position.Y) return position.Y < other.position.Y;
        if (position.Z != other.position.Z) return position.Z < other.position.Z;
        return key < other.key;
    }
};

class KMeans {
private:
    // Centroids of the clusters
    std::vector<AkVector> centroids;
    // Seed for the random number generator
    unsigned int seed;
    // Tolerance is the threshold for the convergence of the algorithm
    float tolerance;
    // Distance threshold is the maximum distance between a point and a centroid to be considered in the same cluster
    float distanceThreshold;
    // Labels for each object
    std::vector<int> labels;
    // Maximum number of clusters
    unsigned int maxClusters;
    // Vector of clusters, each cluster is an unordered_set of ObjectPosition
    std::vector<std::vector<ObjectPosition>> clusters;

    unsigned int determineMaxClusters(unsigned int numObjects) {
        return static_cast<unsigned int>(std::sqrt(numObjects));
    }

    void initializeCentroids(const std::vector<AkVector>& points) {
        std::default_random_engine generator(seed);
        std::uniform_int_distribution<int> distribution(0, points.size() - 1);

        centroids[0] = points[distribution(generator)];
        for (size_t i = 1; i < centroids.size(); ++i) {
            float maxDist = 0;
            AkVector nextCentroid;
            for (const auto& point : points) {
                float minDist = std::numeric_limits<float>::max();
                for (size_t j = 0; j < i; ++j) {
                    float dist = calculateDistance(point, centroids[j]);
                    if (dist < minDist) {
                        minDist = dist;
                    }
                }
                if (minDist > maxDist) {
                    maxDist = minDist;
                    nextCentroid = point;
                }
            }
            centroids[i] = nextCentroid;
        }
    }

    inline float calculateDistance(const AkVector& a, const AkVector& b) const {
        return (a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z);
    }

    bool assignPointsToClusters(const std::vector<ObjectPosition>& objects) {
        bool changed = false;
        std::vector<std::vector<ObjectPosition>> newClusters(centroids.size());

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
            if (labels[i] != closestCentroid) {
                labels[i] = closestCentroid;
                changed = true;
            }
            newClusters[closestCentroid].push_back(objects[i]);
        }

        if (changed) {
            clusters = std::move(newClusters); // Use move semantics to avoid copying
        }

        return changed;
    }

    bool updateCentroids() {
        if (clusters.empty()) return false;

        std::vector<AkVector> new_centroids(centroids.size(), { 0, 0, 0 });
        std::vector<int> counts(centroids.size(), 0);
        bool changed = false;

        for (size_t i = 0; i < clusters.size(); ++i) {
            for (const auto& obj : clusters[i]) {
                new_centroids[i].X += obj.position.X;
                new_centroids[i].Y += obj.position.Y;
                new_centroids[i].Z += obj.position.Z;
                counts[i]++;
            }

            if (counts[i] > 0) {
                AkVector new_centroid = { new_centroids[i].X / counts[i], new_centroids[i].Y / counts[i], new_centroids[i].Z / counts[i] };
                if (calculateDistance(centroids[i], new_centroid) > tolerance * tolerance) {
                    centroids[i] = new_centroid;
                    changed = true;
                }
            }
        }

        return changed;
    }

    void adjustClusterCount(unsigned int numObjects) {
        if (numObjects == 0) {
            return;
        }
        maxClusters = determineMaxClusters(numObjects);
        centroids.resize(maxClusters); // Resize centroids to match the adjusted number of clusters
    }

public:
    KMeans(float tolerance = 0.01f, float distanceThreshold = 100.0f)
        : tolerance(tolerance), distanceThreshold(distanceThreshold) {
        std::random_device rd;
        seed = rd();
    }

    void setTolerance(float newValue) {
        tolerance = clamp(newValue, 0.001f, 1.0f);
    }

    void setDistanceThreshold(float newValue) {
        distanceThreshold = clamp(newValue, 0.1f, 1000.0f);
    }

    void performClustering(const std::vector<ObjectPosition>& objects, unsigned int max_iterations = 20) {
        labels.resize(objects.size(), -1);

        adjustClusterCount(objects.size());

        std::vector<AkVector> positions;
        positions.reserve(objects.size()); // Reserve memory for positions vector
        for (const auto& obj : objects) {
            positions.push_back(obj.position);
        }
        initializeCentroids(positions);

        clusters.resize(centroids.size());

        for (unsigned int i = 0; i < max_iterations; ++i) {
            bool changed = assignPointsToClusters(objects);
            if (!changed || !updateCentroids()) {
                break;
            }
        }

        // Filter out empty clusters
        std::vector<std::vector<ObjectPosition>> nonEmptyClusters;
        for (const auto& cluster : clusters) {
            if (!cluster.empty()) {
                nonEmptyClusters.push_back(cluster);
            }
        }
        clusters = std::move(nonEmptyClusters);

        // Update centroids to match non-empty clusters
        std::vector<AkVector> nonEmptyCentroids;
        for (const auto& cluster : clusters) {
            AkVector centroid = { 0, 0, 0 };
            for (const auto& obj : cluster) {
                centroid.X += obj.position.X;
                centroid.Y += obj.position.Y;
                centroid.Z += obj.position.Z;
            }
            centroid.X /= cluster.size();
            centroid.Y /= cluster.size();
            centroid.Z /= cluster.size();
            nonEmptyCentroids.push_back(centroid);
        }
        centroids = std::move(nonEmptyCentroids);
    }


    const std::vector<int>& getLabels() const {
        return labels;
    }

    const std::vector<AkVector>& getCentroids() const {
        return centroids;
    }

    std::map<AkVector, std::vector<AkAudioObjectID>> getClusters() const {
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

};
