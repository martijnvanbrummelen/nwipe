// prng_analysis.cpp

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <map>
#include <algorithm>
#include <cstring> // For strcmp
#include <filesystem> // C++17 for directory handling

namespace fs = std::filesystem;

const int BYTE_VALUES = 256;

// Structure to hold statistical features
struct StatisticalFeatures {
    std::vector<double> byteFrequencies; // Frequency of each byte value
    double entropy;
    double mean;
    double variance;
};

// Function to read data from a file
bool readData(const std::string& filename, std::vector<unsigned char>& data) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return false;
    }
    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (data.empty()) {
        std::cerr << "Error: File is empty or could not be read: " << filename << std::endl;
        return false;
    }
    return true;
}

// Function to compute statistical features
StatisticalFeatures computeFeatures(const std::vector<unsigned char>& data) {
    StatisticalFeatures features;
    features.byteFrequencies.resize(BYTE_VALUES, 0.0);
    size_t dataSize = data.size();

    // Compute byte frequencies
    for (unsigned char byte : data) {
        features.byteFrequencies[byte]++;
    }
    for (double& freq : features.byteFrequencies) {
        freq /= dataSize;
    }

    // Compute mean and variance
    double sum = 0.0;
    double sumSquared = 0.0;
    for (unsigned char byte : data) {
        sum += byte;
        sumSquared += byte * byte;
    }
    features.mean = sum / dataSize;
    features.variance = (sumSquared / dataSize) - (features.mean * features.mean);

    // Compute entropy
    features.entropy = 0.0;
    for (double freq : features.byteFrequencies) {
        if (freq > 0) {
            features.entropy -= freq * std::log2(freq);
        }
    }

    return features;
}

// Function to compute Euclidean distance between two vectors
double computeEuclideanDistance(const std::vector<double>& vec1, const std::vector<double>& vec2) {
    double sumSquares = 0.0;
    for (size_t i = 0; i < vec1.size(); i++) {
        double diff = vec1[i] - vec2[i];
        sumSquares += diff * diff;
    }
    return std::sqrt(sumSquares);
}

// Function to compare the actual data to generated samples
void compareData(
    const StatisticalFeatures& actualFeatures,
    const std::map<std::string, StatisticalFeatures>& sampleFeaturesMap
) {
    std::map<std::string, double> distances;

    std::cout << "\nComparing actual data to PRNG samples...\n" << std::endl;

    for (const auto& pair : sampleFeaturesMap) {
        const std::string& prngName = pair.first;
        const StatisticalFeatures& sampleFeatures = pair.second;

        // Compute Euclidean distance between byte frequency distributions
        double distance = computeEuclideanDistance(actualFeatures.byteFrequencies, sampleFeatures.byteFrequencies);

        // Optionally, you can also compare other features like entropy, mean, variance
        // For simplicity, we use only byte frequencies here

        distances[prngName] = distance;
        std::cout << "Distance to " << prngName << ": " << distance << std::endl;
    }

    // Find the PRNG with the minimum distance
    auto minElement = std::min_element(
        distances.begin(),
        distances.end(),
        [](const std::pair<std::string, double>& a, const std::pair<std::string, double>& b) {
            return a.second < b.second;
        }
    );

    if (minElement != distances.end()) {
        std::cout << "\nThe actual data is most similar to: " << minElement->first << std::endl;
    } else {
        std::cout << "\nCould not determine the PRNG used to generate the actual data." << std::endl;
    }
}

int main(int argc, char* argv[]) {
    // Define PRNG sample file naming conventions
    const std::map<std::string, std::string> prngSampleFiles = {
        {"Mersenne Twister (mt19937ar-cok)", "Mersenne_Twister__mt19937ar-cok_.bin"},
        {"ISAAC (rand.c 20010626)", "ISAAC__rand.c_20010626_.bin"},
        {"ISAAC-64 (isaac64.c)", "ISAAC-64__isaac64.c_.bin"},
        {"Lagged Fibonacci Generator", "Lagged_Fibonacci_Generator.bin"},
        {"XORoshiro-256", "XORoshiro-256.bin"},
        {"AES-256-CTR (OpenSSL)", "AES-256-CTR__OpenSSL_.bin"}
    };

    // Check for correct usage
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <unknown_data_file> [sample_files_directory]" << std::endl;
        std::cerr << "Example: " << argv[0] << " actual_data.bin ./prng_samples/" << std::endl;
        return EXIT_FAILURE;
    }

    std::string unknownDataFile = argv[1];
    std::string sampleFilesDir = "."; // Default to current directory

    if (argc == 3) {
        sampleFilesDir = argv[2];
        // Check if the directory exists
        if (!fs::is_directory(sampleFilesDir)) {
            std::cerr << "Error: Sample files directory does not exist: " << sampleFilesDir << std::endl;
            return EXIT_FAILURE;
        }
    }

    // Read and compute features for each PRNG sample
    std::map<std::string, StatisticalFeatures> sampleFeaturesMap;

    std::cout << "Reading PRNG sample files from directory: " << sampleFilesDir << std::endl;

    for (const auto& prngPair : prngSampleFiles) {
        const std::string& prngName = prngPair.first;
        std::string sampleFilename = prngPair.second;

        // If a directory is specified, prepend it to the filename
        if (!sampleFilesDir.empty() && sampleFilesDir != ".") {
            sampleFilename = fs::path(sampleFilesDir) / sampleFilename;
        }

        std::vector<unsigned char> sampleData;
        std::cout << "Processing sample file: " << sampleFilename << " for PRNG: " << prngName << std::endl;

        if (!readData(sampleFilename, sampleData)) {
            std::cerr << "Warning: Skipping PRNG: " << prngName << std::endl;
            continue;
        }

        StatisticalFeatures features = computeFeatures(sampleData);
        sampleFeaturesMap[prngName] = features;
    }

    if (sampleFeaturesMap.empty()) {
        std::cerr << "Error: No valid PRNG sample files were processed. Exiting." << std::endl;
        return EXIT_FAILURE;
    }

    // Read and compute features for the actual data
    std::vector<unsigned char> actualData;
    std::cout << "\nReading unknown data file: " << unknownDataFile << std::endl;

    if (!readData(unknownDataFile, actualData)) {
        std::cerr << "Error: Could not read the unknown data file. Exiting." << std::endl;
        return EXIT_FAILURE;
    }

    StatisticalFeatures actualFeatures = computeFeatures(actualData);

    // Display features of the actual data (optional)
    /*
    std::cout << "\nStatistical Features of Actual Data:" << std::endl;
    std::cout << "Mean: " << actualFeatures.mean << std::endl;
    std::cout << "Variance: " << actualFeatures.variance << std::endl;
    std::cout << "Entropy: " << actualFeatures.entropy << " bits" << std::endl;
    */

    // Compare the actual data to the PRNG samples
    compareData(actualFeatures, sampleFeaturesMap);

    return EXIT_SUCCESS;
}
