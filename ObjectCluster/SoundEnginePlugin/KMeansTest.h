
#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <limits>
#include <cmath>

struct Point {
    double x, y, z;
};

class KMeans {
private:
    std::vector<Point> centroids;
    unsigned int seed;
    double tolerance;
    std::vector<int> labels; // Stores the label for each point
    std::vector<std::vector<Point>> clusters; // store points in each cluster

public:
    KMeans(unsigned int n_clusters, double tolerance = 0.001)
        : centroids(n_clusters), tolerance(tolerance) {
        std::random_device rd;
        seed = rd();
    }

    void initializeCentroids(const std::vector<Point>& points) {
        std::default_random_engine generator(seed);
        std::uniform_int_distribution<int> distribution(0, points.size() - 1);

        for (auto& centroid : centroids) {
            centroid = points[distribution(generator)];
        }
    }

    double calculateDistance(const Point& a, const Point& b) {
        return std::sqrt(std::pow(a.x - b.x, 2) + std::pow(a.y - b.y, 2) + std::pow(a.z - b.z, 2));
    }

    bool assignPointsToClusters(const std::vector<Point>& points, std::vector<int>& labels) {
        bool changed = false;
        clusters.clear(); // Clear previous cluster assignments
        clusters.resize(centroids.size()); // Prepare clusters for new assignments

        for (size_t i = 0; i < points.size(); ++i) {
            double min_distance = std::numeric_limits<double>::max();
            int closest_centroid = -1;
            for (size_t j = 0; j < centroids.size(); ++j) {
                double distance = calculateDistance(points[i], centroids[j]);
                if (distance < min_distance) {
                    min_distance = distance;
                    closest_centroid = j;
                }
            }
            if (labels[i] != closest_centroid) {
                labels[i] = closest_centroid;
                changed = true;
            }
            clusters[closest_centroid].push_back(points[i]); // Assign point to its cluster
        }
        return changed;
    }

    bool updateCentroids(const std::vector<Point>& points, const std::vector<int>& labels) {
        std::vector<Point> new_centroids(centroids.size(), { 0, 0, 0 });
        std::vector<int> counts(centroids.size(), 0);
        for (size_t i = 0; i < points.size(); ++i) {
            new_centroids[labels[i]].x += points[i].x;
            new_centroids[labels[i]].y += points[i].y;
            new_centroids[labels[i]].z += points[i].z;
            counts[labels[i]]++;
        }

        bool changed = false;
        for (size_t i = 0; i < centroids.size(); ++i) {
            if (counts[i] > 0) {
                Point new_centroid = { new_centroids[i].x / counts[i], new_centroids[i].y / counts[i], new_centroids[i].z / counts[i] };
                if (calculateDistance(centroids[i], new_centroid) > tolerance) {
                    centroids[i] = new_centroid;
                    changed = true;
                }
            }
        }
        return changed;
    }

    void fit(const std::vector<Point>& points, unsigned int max_iterations = 100) {
        labels.resize(points.size(), -1);
        initializeCentroids(points);

        for (unsigned int i = 0; i < max_iterations; ++i) {
            bool changed = assignPointsToClusters(points, labels);
            if (!changed || !updateCentroids(points, labels)) {
                break; // Convergence achieved
            }
        }
    }

    const std::vector<int>& getLabels() const {
        return labels;
    }

    const std::vector<std::vector<Point>>& getClusters() const {
        return clusters;
    }
};


int main() {

    std::vector<Point> sample_in_0 = {
        {-11.4860,    5.5058,   -9.0644},
        {-11.1426,    4.8875,   -8.1357},
        { 10.9231,    0.3666,   -3.5779},
        {-12.9427,    8.3412,   -5.9818},
        {-10.0903,    5.3482,   -8.7261},
        {-11.5049,    6.7360,   -7.7034},
        {  5.7238,  -14.0290,    4.2147},
        {  5.2085,  -15.4755,    3.4246},
        { 11.2001,    0.9678,   -1.0240},
        { 11.3687,   -0.6454,   -3.8518},
        { 11.0813,    0.1786,   -3.1050},
        {  6.5185,  -13.3054,    4.1873},
        { 11.4567,    0.0249,   -3.0829},
        {  7.1574,  -14.5416,    3.9704},
        {  8.4637,  -13.5819,    2.3469},
    };

    std::vector<Point> sample_in_1 = {
        { -3.3311,  -17.2487,    0.6263},
        { -2.5003,  -19.0799,   -1.0433},
        { -2.5524,  -20.3855,   -1.0302},
        { 12.2500,   -0.1451,   -0.7969},
        { 12.8404,   -0.2285,    1.5140},
        {-25.5839,    0.3063,    6.1142},
        {-24.7935,    0.5205,    5.9570},
        { -3.0146,  -18.5141,   -1.0926},
        { 10.1120,   -1.2544,   -0.1439},
        {-23.4916,   -0.8105,    6.6513},
        { -5.4538,  -20.6453,    0.1573},
        { 10.0714,   -1.0966,   -1.0421},
        {-24.5679,    2.1275,    5.7701},
        { 11.2001,   -1.5779,    1.0244},
        {-24.4899,   -0.2897,    7.3753},
    };

    // Initialize
    KMeans kmeans_0(6);
    KMeans kmeans_1(6);

    // Fit test vectors
    kmeans_0.fit(sample_in_0);
    kmeans_1.fit(sample_in_1);

    auto printClusters = [](const KMeans& kmeans) {
        const auto& clusters = kmeans.getClusters();
        for (size_t i = 0; i < clusters.size(); ++i) {
            std::cout << "Cluster " << i << ":\n";
            for (const auto& point : clusters[i]) {
                std::cout << "    (" << point.x << ", " << point.y << ", " << point.z << ")\n";
            }
        }
        };

    std::cout << "Clusters for sample_in_0:\n";
    printClusters(kmeans_0);

    std::cout << "\nClusters for sample_in_1:\n";
    printClusters(kmeans_1);
}

