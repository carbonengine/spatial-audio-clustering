#include "KMeans.h"

template <typename T>
T clamp(T value, T min, T max) {
    return std::max(min, std::min(max, value));
}
unsigned int KMeans::determineMaxClusters(unsigned int numObjects) {
    return static_cast<unsigned int>(std::sqrt(numObjects));
}

void KMeans::initializeCentroids(const std::vector<AkVector>& points) {
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(0, points.size() - 1);

    centroids[0] = points[distribution(generator)];

    for (size_t k = 1; k < centroids.size(); ++k) {
        std::vector<float> distances(points.size(), std::numeric_limits<float>::max());

        for (size_t i = 0; i < points.size(); ++i) {
            for (size_t j = 0; j < k; ++j) {
                float dist = calculateDistance(points[i], centroids[j]);
                distances[i] = std::min(distances[i], dist);
            }
        }

        for (auto& dist : distances) {
            dist = dist * dist;
        }

        std::discrete_distribution<> weighted_distribution(distances.begin(), distances.end());
        centroids[k] = points[weighted_distribution(generator)];
    }
}

float KMeans::calculateDistance(const AkVector& a, const AkVector& b) const {
    return (a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y) + (a.Z - b.Z) * (a.Z - b.Z);
}

bool KMeans::assignPointsToClusters(const std::vector<ObjectPosition>& objects) {
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

    clusters = std::move(newClusters);
    return changed;
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

float KMeans::calculateSSE() const
{
    float sse = 0.0f;
    for (size_t i = 0; i < clusters.size(); ++i) {
        for (const auto& obj : clusters[i]) {
            sse += std::pow(calculateDistance(obj.position, centroids[i]), 2);
        }
    }
    return sse;
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
    adjustClusterCount(objects.size());

    std::vector<AkVector> positions;
    positions.reserve(objects.size());
    for (const auto& obj : objects) {
        positions.push_back(obj.position);
    }
    initializeCentroids(positions);

    float prev_sse = std::numeric_limits<float>::max();
    for (unsigned int i = 0; i < max_iterations; ++i) {
        bool changed = assignPointsToClusters(objects);
        bool centroidsUpdated = updateCentroids();

        float current_sse = calculateSSE();
        sse_values.push_back(current_sse);

        // Check for convergence
        if ((!changed && !centroidsUpdated) ||
            (std::abs(prev_sse - current_sse) < tolerance * prev_sse)) {
            break;
        }
        prev_sse = current_sse;
    }

    // Filter out empty clusters
    std::vector<std::vector<ObjectPosition>> nonEmptyClusters;
    std::vector<AkVector> nonEmptyCentroids;
    for (size_t i = 0; i < clusters.size(); ++i) {
        if (!clusters[i].empty()) {
            nonEmptyClusters.push_back(clusters[i]);
            nonEmptyCentroids.push_back(centroids[i]);
        }
    }
    clusters = std::move(nonEmptyClusters);
    centroids = std::move(nonEmptyCentroids);
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