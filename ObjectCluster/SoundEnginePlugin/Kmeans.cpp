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

    if (changed) {
        clusters = std::move(newClusters);
    }

    return changed;
}

bool KMeans::updateCentroids() {
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

void KMeans::adjustClusterCount(unsigned int numObjects) {
    if (numObjects == 0) {
        return;
    }
    maxClusters = determineMaxClusters(numObjects);
    centroids.resize(maxClusters);
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

    clusters.resize(centroids.size());

    for (unsigned int i = 0; i < max_iterations; ++i) {
        bool changed = assignPointsToClusters(objects);
        if (!changed || !updateCentroids()) {
            break;
        }
    }

    std::vector<std::vector<ObjectPosition>> nonEmptyClusters;
    for (const auto& cluster : clusters) {
        if (!cluster.empty()) {
            nonEmptyClusters.push_back(cluster);
        }
    }
    clusters = std::move(nonEmptyClusters);

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

const std::vector<int>& KMeans::getLabels() const {
    return labels;
}

const std::vector<AkVector>& KMeans::getCentroids() const {
    return centroids;
}

std::map<AkTransform, std::vector<AkAudioObjectID>> KMeans::getClusters() const {
    std::map<AkTransform, std::vector<AkAudioObjectID>> clusterMap;

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

            AkTransform centroidTransform;
            centroidTransform.SetOrientation(AkVector{ 1, 0, 0 }, AkVector{ 0, 1, 0 });
            centroidTransform.SetPosition(centroid);

            std::vector<AkAudioObjectID> objectIDs;
            for (const auto& obj : cluster) {
                objectIDs.push_back(obj.key);
            }

            clusterMap[centroidTransform] = std::move(objectIDs);
        }
    }

    return clusterMap;
}