#pragma once
#include <vector>
#include <random>
#include <limits>
#include <cmath>
#include <algorithm>
#include <map>
#include <AK/SoundEngine/Common/AkTypes.h>
#include <AK/SoundEngine/Common/AkCommonDefs.h>
#include <AK/Plugin/PluginServices/AkMixerInputMap.h>
#include "Utilities.h"

#undef min
#undef max

/**
 * @brief Clamps a value between a minimum and maximum.
 * @tparam T The type of the value and bounds.
 * @param value The value to clamp.
 * @param min The minimum allowed value.
 * @param max The maximum allowed value.
 * @return The clamped value.
 */
template <typename T>
T clamp(T value, T min, T max);

/**
 * @brief Compares two AkVector objects.
 *
 * @param a The first AkVector to compare.
 * @param b The second AkVector to compare.
 * @return true if a is considered less than b, false otherwise.
 */
inline bool operator<(const AkVector& a, const AkVector& b) {
    if (a.X != b.X) return a.X < b.X;
    if (a.Y != b.Y) return a.Y < b.Y;
    return a.Z < b.Z;
}

/**
 * @brief Represents a position and key for an audio object.
 */
struct ObjectPosition {
    AkVector position; ///< The 3D position of the audio object.
    AkAudioObjectID key; ///< The unique identifier of the audio object.
};

/**
 * @brief Holds metadata about an object used during cluster initialization.
 */
struct ObjectMetadata {
    ObjectPosition object;
    float localDensity;    // Density of objects in local neighborhood
    float minDistanceToCentroid; // Min distance to any existing centroid
};

/**
 * @brief Implements the K-means clustering algorithm for audio objects in 3D space.
 *
 * K-means clustering is an unsupervised machine learning algorithm that groups similar data points
 * into a specified number (K) of clusters. It achieves this by iteratively assigning each data point
 * to the cluster with the nearest mean (centroid) and then recalculating the cluster means until convergence.
 * K-means is commonly used in data classification and pattern recognition.
 *
 * In the context of our use case, we adapt the algorithm to work in 3D space for audio object clustering.
 * The implemented algorithm follows these steps:
 * 1. **Initialization:** Determines centroids based on local density, with special handling for objects
 *    near the origin. Additional centroids are selected using k-means++ approach, maximizing distances
 *    between centroids while respecting a distance threshold.
 * 2. **Assignment:** Each audio object is assigned to the nearest centroid if within the specified
 *    distance threshold. Objects beyond the threshold are marked as unassigned.
 * 3. **Update Step:** The centroid of each cluster is recalculated as the average position of all
 *    audio objects assigned to it. Empty clusters are removed, and new clusters may form from
 *    unassigned points if they are sufficiently close together.
 * 4. **Convergence:** This iterative process continues until either cluster assignments stabilize,
 *    the change in Sum of Squared Errors (SSE) falls below the tolerance threshold, or the maximum
 *    iterations are reached.
 * 5. **Result:** The final result is a dynamic set of clusters, each represented by its centroid
 *    position. The number of clusters can vary based on the spatial distribution of audio objects
 *    and the distance threshold constraints.
 */

class KMeans {
private:

    unsigned int maxClusters; ///< Maximum number of clusters.
    unsigned int seed; ///< Seed for the random number generator.
    float m_tolerance; ///< Tolerance for convergence.
    float m_distanceThreshold; ///< Maximum distance for a point to be considered in a cluster.
    float m_minThreshold; ///< Minimun value for the distance threshold.
    float m_maxThreshold; ///< Maximum value for the distance threshold.
    std::vector<AkVector> centroids; ///< The centroids of the clusters.
    std::vector<int> labels; ///< Labels assigning each point to a cluster.
    std::vector<std::vector<ObjectPosition>> clusters; ///< The resulting clusters.
    std::vector<float> sse_values; /// Sum of squared errors values
    std::vector<ObjectPosition> unassignedPoints; /// A vector for unassigned points.
    Utilities m_utilities;


    /**
     * @brief Retrieves points that couldn't be assigned to any cluster due to distance constraints.
     * @return A const reference to the vector of unassigned object positions.
     */
    const std::vector<ObjectPosition>& KMeans::getUnassignedPoints() const {
        return unassignedPoints;
    }

    /**
     * @brief Determines the maximum number of clusters based on the number of objects.
     * @param numObjects The number of objects to cluster.
     * @return The maximum number of clusters.
     */
    unsigned int determineMaxClusters(unsigned int numObjects);

    /**
     * @brief Initializes the centroids for the K-means algorithm.
     * @param points The points to initialize centroids from.
     */
    void initializeCentroids(const std::vector<ObjectPosition>& objects);

    /**
     * @brief Calculates the squared distance between two points.
     * @param a The first point.
     * @param b The second point.
     * @return The squared distance between the points.
     */
    float calculateDistance(const AkVector& a, const AkVector& b) const;

    /**
     * @brief Assigns points to the nearest cluster.
     * @param objects The objects to assign to clusters.
     * @return True if any assignments changed, false otherwise.
     */
    bool assignPointsToClusters(const std::vector<ObjectPosition>& objects);

    /**
     * @brief Updates the centroids based on the current cluster assignments.
     * @return True if any centroid changed significantly, false otherwise.
     */
    bool updateCentroids();

    /**
     * @brief Adjusts the number of clusters based on the number of objects.
     * @param numObjects The number of objects to cluster.
     */
    void adjustClusterCount(unsigned int numObjects);

    /**
     * @brief Calculates the Sum of Squared Errors (SSE) for the current clustering.
     *
     * The Sum of Squared Errors is a measure of the quality of the clustering. It is calculated
     * by summing the squared Euclidean distances between each data point and its assigned
     * centroid. A lower SSE indicates a better clustering result, as it means the data points
     * are closer to their centroids on average.
     *
     * @return The calculated Sum of Squared Errors for the current clustering.
     */
    float calculateSSE() const;

    /**
     * @brief Calculates the centroid of a cluster of object positions.
     *
     * This function computes the average position (centroid) of all objects in a given cluster.
     * If the cluster is empty, it returns a zero vector.
     *
     * @param cluster A vector of ObjectPosition representing a cluster of objects.
     * @return AkVector The calculated centroid of the cluster.
     *
     */

    AkVector calculateCentroid(const std::vector<ObjectPosition>& cluster);

    /**
     * @brief Adjusts the number and composition of clusters based on the distance threshold.
     *
     * This method performs the following operations:
     * 1. Removes empty clusters to optimize cluster count.
     * 2. Attempts to create new clusters from unassigned points, respecting the distance threshold.
    */
    void adjustClusterCount();

    /**
     * @brief Calculates the Gaussian weight based on squared distance and radius
     * @param distanceSquared The squared distance between two points
     * @param radiusSquared The squared radius of influence
     * @return The calculated weight
     */

    float calculateGaussianWeight(float distanceSquared, float radiusSquared) const;
    


public:
    /**
     * @brief Constructs a KMeans instance.
     * @param tolerance The convergence tolerance.
     * @param distanceThreshold The maximum distance for a point to be in a cluster.
     */
    KMeans(float tolerance = 0.01f,
        float distanceThreshold = 100.0f,
        float minDistanceThreshold = 10.f,
        float maxDistanceThreshold = 1000.f);

    /**
     * @brief Sets the convergence tolerance.
     * @param newValue The new tolerance value.
     */
    void setTolerance(float newValue);

    /**
     * @brief Sets the distance threshold.
     * @param newValue The new distance threshold value.
     */
    void setDistanceThreshold(float newValue);

    /**
     * @brief Sets the internal minimun distance threshold value.
    */
    void setMinDistanceThreshold(float newValue);

    /**
     * @brief Sets the internal maximum distance threshold value.
    */
    void setMaxDistanceThreshold(float newValue);

    /**
     * @brief Performs K-means clustering on the given objects.
     * @param objects The objects to cluster.
     * @param max_iterations The maximum number of iterations.
     */
    void performClustering(const std::vector<ObjectPosition>& objects, unsigned int max_iterations = 20);

    /**
     * @brief Gets the cluster labels for each object.
     * @return The cluster labels.
     */
    const std::vector<int>& getLabels() const;

    /**
     * @brief Gets the centroids of the clusters.
     * @return The centroids.
     */
    const std::vector<AkVector>& getCentroids() const;

    /**
     * @brief Gets the resulting clusters as a map of AkVectors to object IDs.
     * @return The clusters.
     */
    std::map<AkVector, std::vector<AkAudioObjectID>> getClusters() const;
};