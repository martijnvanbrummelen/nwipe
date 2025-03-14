Certainly! Below is the updated `README.md` for the **PRNG Analysis Tool**, augmented with information indicating that PRNG samples can be created and utilized using the **PRNG Benchmark** tool.

---

# PRNG Analysis Tool

![PRNG Analysis](https://img.shields.io/badge/license-GPLv3-blue.svg)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)

## Table of Contents

- [Introduction](#introduction)
- [Features](#features)
- [Supported PRNGs](#supported-prngs)
- [Installation](#installation)
  - [Prerequisites](#prerequisites)
  - [Compiling the Program](#compiling-the-program)
- [Usage](#usage)
  - [Command-Line Arguments](#command-line-arguments)
  - [Example Usage](#example-usage)
  - [Generating and Using PRNG Samples](#generating-and-using-prng-samples)
- [Statistical Analysis](#statistical-analysis)
- [License](#license)
- [Contributing](#contributing)

## Introduction

The **PRNG Analysis Tool** is a sophisticated C++ application designed to analyze and identify the probable pseudorandom number generator (PRNG) used to generate a given dataset. By computing various statistical features of the input data and comparing them against known PRNG samples, the tool assists in determining the underlying PRNG mechanism.

## Features

- **Comprehensive Statistical Analysis**: Computes byte frequencies, entropy, mean, and variance of the input data.
- **PRNG Comparison**: Compares the analyzed data against multiple known PRNG samples to identify the most similar generator.
- **Flexible Sample Handling**: Supports specifying a directory containing PRNG sample files.
- **User-Friendly Output**: Provides clear and concise distance metrics to indicate similarity between the input data and PRNG samples.
- **Efficient Compilation**: Includes a `Makefile` for easy compilation and management.
- **Integration with PRNG Benchmark**: Utilize the **PRNG Benchmark** tool to generate high-quality PRNG samples for analysis.

## Supported PRNGs

The tool currently supports analysis against the following PRNGs:

1. **Mersenne Twister (mt19937ar-cok)**
2. **ISAAC (rand.c 20010626)**
3. **ISAAC-64 (isaac64.c)**
4. **Lagged Fibonacci Generator**
5. **XORoshiro-256**
6. **AES-256-CTR (OpenSSL)**

Each PRNG sample is expected to be provided as a binary file with specific naming conventions (detailed below).

## Installation

### Prerequisites

- **C++ Compiler**: Ensure you have `g++` version 7 or higher installed, which supports C++17.
- **Make**: Utility for managing the build process.

To check your `g++` version:

```bash
g++ --version
```

Ensure it outputs version 7 or higher.

### Compiling the Program

1. **Clone the Repository**: (If applicable)

   ```bash
   git clone https://github.com/FabianDruschke/prng_analysis.git
   cd prng_analysis
   ```

2. **Prepare Source Files**: Ensure that `prng_analysis.cpp` and the `Makefile` are present in the directory.

3. **Compile Using Makefile**:

   ```bash
   make
   ```

   This command compiles the source code and generates the executable named `prng_analysis`.

4. **Clean Build Files**:

   To remove the compiled executable and object files, use:

   ```bash
   make clean
   ```

## Usage

### Command-Line Arguments

```bash
./prng_analysis <unknown_data_file> [sample_files_directory]
```

- `<unknown_data_file>`: Path to the binary file containing the data you wish to analyze.
- `[sample_files_directory]` (optional): Path to the directory containing PRNG sample binary files. If not specified, the current directory is used by default.

### Example Usage

1. **Basic Usage**:

   Analyze `actual_data.bin` using PRNG samples located in `./prng_samples/` directory.

   ```bash
   ./prng_analysis actual_data.bin ./prng_samples/
   ```

2. **Using Default Sample Directory**:

   If your PRNG sample files are in the current directory:

   ```bash
   ./prng_analysis actual_data.bin
   ```

### Generating and Using PRNG Samples

To effectively utilize the **PRNG Analysis Tool**, you need high-quality PRNG samples. The **PRNG Benchmark** tool facilitates the creation of these samples. Follow the steps below to generate and integrate PRNG samples into your analysis workflow.

1. **Install PRNG Benchmark**:

   Ensure that you have the **PRNG Benchmark** tool installed. If not, follow its [Installation](https://github.com/Knogle/prng-benchmark#installation) instructions to set it up.

2. **Generate PRNG Samples**:

   Use the **PRNG Benchmark** tool to generate sample binary files for each supported PRNG.

   ```bash
   ./prng_benchmark --memonly --size 100
   ```

   - The above command generates 100 MB of random data for each supported PRNG in memory-only mode.
   - The generated files will be named following the convention: `<PRNG_Name>.bin` (e.g., `Mersenne_Twister_mt19937ar-cok.bin`).

3. **Organize Sample Files**:

   Create a dedicated directory to store all PRNG sample files.

   ```bash
   mkdir prng_samples
   mv *.bin prng_samples/
   ```

4. **Run PRNG Analysis**:

   With the samples generated and organized, execute the **PRNG Analysis Tool** to analyze your unknown dataset against the samples.

   ```bash
   ./prng_analysis actual_data.bin ./prng_samples/
   ```

   **Sample Output**:

   ```
   Reading PRNG sample files from directory: ./prng_samples/
   Processing sample file: ./prng_samples/Mersenne_Twister_mt19937ar-cok.bin for PRNG: Mersenne Twister (mt19937ar-cok)
   Processing sample file: ./prng_samples/ISAAC_rand.c_20010626_.bin for PRNG: ISAAC (rand.c 20010626)
   Processing sample file: ./prng_samples/ISAAC-64_isaac64.c_.bin for PRNG: ISAAC-64 (isaac64.c)
   Processing sample file: ./prng_samples/Lagged_Fibonacci_Generator.bin for PRNG: Lagged Fibonacci Generator
   Processing sample file: ./prng_samples/XORoshiro-256.bin for PRNG: XORoshiro-256
   Processing sample file: ./prng_samples/AES-256-CTR_OpenSSL_.bin for PRNG: AES-256-CTR (OpenSSL)

   Reading unknown data file: actual_data.bin

   Comparing actual data to PRNG samples...

   Distance to Mersenne Twister (mt19937ar-cok): 12.3456
   Distance to ISAAC (rand.c 20010626): 23.4567
   Distance to ISAAC-64 (isaac64.c): 34.5678
   Distance to Lagged Fibonacci Generator: 45.6789
   Distance to XORoshiro-256: 56.7890
   Distance to AES-256-CTR (OpenSSL): 67.8901

   The actual data is most similar to: Mersenne Twister (mt19937ar-cok)
   ```

## Statistical Analysis

The tool computes the following statistical features from the input data:

1. **Byte Frequencies**: Calculates the frequency of each byte value (0-255) in the dataset.
2. **Entropy**: Measures the randomness within the data based on byte frequency distributions.
3. **Mean**: Computes the average byte value.
4. **Variance**: Assesses the variability of byte values around the mean.

These features are then compared against the same metrics computed from known PRNG samples using the Euclidean distance metric. The PRNG with the smallest distance is considered the most similar to the input data.

## License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**. See the [LICENSE](LICENSE) file for details.

### GNU General Public License v3.0

```
                        GNU GENERAL PUBLIC LICENSE
                           Version 3, 29 June 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed.

 [Full license text continues...]
```

For the complete license text, refer to the [LICENSE](LICENSE) file in the repository or visit [https://www.gnu.org/licenses/gpl-3.0.en.html](https://www.gnu.org/licenses/gpl-3.0.en.html).

## Contributing

Contributions are welcome! Please follow these steps to contribute:

1. **Fork the Repository**: Click the "Fork" button on the repository page.

2. **Clone Your Fork**:

   ```bash
   git clone https://github.com/yourusername/prng_analysis.git
   cd prng_analysis
   ```

3. **Create a New Branch**:

   ```bash
   git checkout -b feature/your-feature-name
   ```

4. **Make Your Changes**: Implement your feature or bug fix.

5. **Commit Your Changes**:

   ```bash
   git commit -m "Description of your changes"
   ```

6. **Push to Your Fork**:

   ```bash
   git push origin feature/your-feature-name
   ```

7. **Create a Pull Request**: Navigate to the original repository and create a pull request from your fork.


