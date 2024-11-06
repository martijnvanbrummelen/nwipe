# PRNG Benchmark

![License: GPLv3](https://img.shields.io/badge/License-GPLv3-blue.svg)

## Table of Contents
- [Overview](#overview)
- [Features](#features)
- [Supported PRNGs](#supported-prngs)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
  - [Cloning the Repository](#cloning-the-repository)
  - [Building the Project](#building-the-project)
- [Usage](#usage)
  - [Command-Line Parameters](#command-line-parameters)
  - [Examples](#examples)
- [Benchmark Output](#benchmark-output)
- [License](#license)
- [Contributing](#contributing)
- [Acknowledgements](#acknowledgements)
- [Contact](#contact)

## Overview

**PRNG Benchmark** is a robust and flexible tool designed to evaluate and compare the performance of various pseudorandom number generators (PRNGs). By measuring the time taken and throughput in megabytes per second (MB/s) to generate large volumes of random data, this tool aids developers, researchers, and security professionals in selecting the most suitable PRNG for their specific applications.

## Features

- **Comprehensive PRNG Support:** Benchmarks a diverse set of PRNGs including:
  - Mersenne Twister (`mt19937ar-cok`)
  - ISAAC (`rand.c 20010626`)
  - ISAAC-64 (`isaac64.c`)
  - Lagged Fibonacci Generator
  - XORoshiro-256
  - AES-256-CTR (OpenSSL)
  
- **Flexible Benchmark Size:** Customize the amount of data to generate using the `--size` parameter.

- **Memory-Only Mode:** Utilize the `--memonly` flag to perform benchmarks entirely in memory, eliminating disk I/O for faster results.

- **Detailed Reporting:** Outputs benchmark results displaying elapsed time and throughput (MB/s) for each PRNG.

- **Extensible Design:** Easily add support for additional PRNGs by adhering to the defined interface.

- **Makefile Integration:** Simplifies the build process with a comprehensive `Makefile`.

## Supported PRNGs

| PRNG Name                          | Description                                                                      |
|------------------------------------|----------------------------------------------------------------------------------|
| **Mersenne Twister (`mt19937ar-cok`)** | A widely used PRNG known for its long period and excellent statistical properties. |
| **ISAAC (`rand.c 20010626`)**          | A cryptographically secure PRNG designed for security-sensitive applications.    |
| **ISAAC-64 (`isaac64.c`)**             | A 64-bit variant of the ISAAC PRNG, offering enhanced performance on 64-bit systems. |
| **Lagged Fibonacci Generator**         | A PRNG based on the lagged Fibonacci sequence, balancing speed and randomness quality. |
| **XORoshiro-256**                      | A fast PRNG with excellent statistical properties and a long period.             |
| **AES-256-CTR (OpenSSL)**              | A cryptographically secure PRNG using AES-256 in Counter (CTR) mode.             |

## Prerequisites

Before building and running the **PRNG Benchmark**, ensure that your system meets the following prerequisites:

- **Operating System:** Unix-like systems (Linux, macOS) are recommended. Windows compatibility may require additional configurations.
  
- **C Compiler:** GCC or Clang with C11 support.
  
- **Make:** GNU Make for building the project using the provided `Makefile`.
  
- **Libraries:**
  - **OpenSSL:** Required for the AES-256-CTR PRNG.
    - **Debian/Ubuntu:**
      ```bash
      sudo apt-get update
      sudo apt-get install libssl-dev
      ```
    - **Red Hat/CentOS/Fedora:**
      ```bash
      sudo dnf install openssl-devel
      ```
    - **macOS (using Homebrew):**
      ```bash
      brew install openssl
      ```
  - **Math Library:** Typically included by default (`-lm`).

## Installation

### Cloning the Repository

Start by cloning the repository to your local machine:

```bash
git clone https://github.com/Knogle/prng-benchmark.git
cd prng-benchmark
```

### Building the Project

The project includes a comprehensive `Makefile` to streamline the build process.

1. **Ensure All Dependencies Are Met:**

   Make sure that all required libraries (e.g., OpenSSL) are installed as per the [Prerequisites](#prerequisites) section.

2. **Compile the Project:**

   Simply run:

   ```bash
   make
   ```

   This command will:

   - Compile each `.c` source file into an `.o` object file.
   - Link all object files into the final executable named `prng_benchmark`.

3. **Optional: Parallel Compilation**

   To speed up the build process on multi-core systems, you can leverage parallel jobs:

   ```bash
   make -j$(nproc)
   ```

   *`$(nproc)` automatically detects the number of available CPU cores.*

4. **Verify the Build:**

   After successful compilation, verify the presence of the executable:

   ```bash
   ls -l prng_benchmark
   ```

   You should see the `prng_benchmark` executable with execute permissions.

## Usage

The **PRNG Benchmark** tool offers flexibility through various command-line parameters, allowing users to customize the benchmarking process according to their needs.

### Command-Line Parameters

| Parameter        | Description                                                                            | Default       |
|------------------|----------------------------------------------------------------------------------------|---------------|
| `--memonly`      | Run the benchmark in memory-only mode. Random data will **not** be written to disk.    | Disabled      |
| `--size <MB>`    | Specify the size of data to generate in megabytes (MB). Maximum size depends on system memory. | `100` MB      |
| `-h, --help`     | Display help information.                                                              |               |

### Detailed Explanation

1. **`--memonly`**

   - **Purpose:** Executes the benchmark without writing the generated random data to disk, thereby reducing I/O overhead and potentially increasing benchmark speed.
   
   - **Usage:** Place the `--memonly` flag anywhere in the command.

2. **`--size <MB>`**

   - **Purpose:** Allows users to define the amount of random data to generate for the benchmark.
   
   - **Usage:** Follow the `--size` flag with a positive integer representing the size in megabytes.
   
   - **Example:** `--size 500` to generate 500 MB of data.
   
   - **Constraints:** Ensure that your system has sufficient memory to handle the specified size.

3. **`-h, --help`**

   - **Purpose:** Displays help information detailing usage and available command-line parameters.
   
   - **Usage:** Include either `-h` or `--help` to view the help message.

### Examples

1. **Default Benchmark (100 MB, Writing to Disk):**

   ```bash
   ./prng_benchmark
   ```

   **Output:**
   ```
   Initializing PRNG: Mersenne Twister (mt19937ar-cok)
   Initializing Mersenne Twister PRNG
   Random data written to Mersenne_Twister_mt19937ar-cok.bin.
   Initializing PRNG: ISAAC (rand.c 20010626)
   Initializing ISAAC PRNG
   Random data written to ISAAC_rand_c_20010626.bin.
   ...
   
   Benchmark Results for PRNGs (generated 100 MB):
   PRNG                                     : Seconds    : MB/s     
   ---------------------------------------------------------------------
   Mersenne Twister (mt19937ar-cok)         : 0.350000   : 285.71    
   ISAAC (rand.c 20010626)                  : 0.400000   : 250.00    
   ISAAC-64 (isaac64.c)                     : 0.450000   : 222.22    
   Lagged Fibonacci Generator               : 0.300000   : 333.33    
   XORoshiro-256                            : 0.250000   : 400.00    
   AES-256-CTR (OpenSSL)                    : 0.500000   : 200.00    
    
   The fastest PRNG is: XORoshiro-256 with 0.250000 seconds (400.00 MB/s).
   ```

2. **Memory-Only Mode (100 MB):**

   ```bash
   ./prng_benchmark --memonly
   ```

   **Output:**
   ```
   Benchmarking in memory-only mode. Random data will not be written to disk.
   
   Initializing PRNG: Mersenne Twister (mt19937ar-cok)
   Initializing Mersenne Twister PRNG
   Initializing PRNG: ISAAC (rand.c 20010626)
   Initializing ISAAC PRNG
   ...
   
   Benchmark Results for PRNGs (generated 100 MB):
   PRNG                                     : Seconds    : MB/s     
   ---------------------------------------------------------------------
   Mersenne Twister (mt19937ar-cok)         : 0.350000   : 285.71    
   ISAAC (rand.c 20010626)                  : 0.400000   : 250.00    
   ISAAC-64 (isaac64.c)                     : 0.450000   : 222.22    
   Lagged Fibonacci Generator               : 0.300000   : 333.33    
   XORoshiro-256                            : 0.250000   : 400.00    
   AES-256-CTR (OpenSSL)                    : 0.500000   : 200.00    
    
   The fastest PRNG is: XORoshiro-256 with 0.250000 seconds (400.00 MB/s).
   ```

3. **Custom Benchmark Size (e.g., 500 MB, Writing to Disk):**

   ```bash
   ./prng_benchmark --size 500
   ```

   **Output:**
   ```
   Initializing PRNG: Mersenne Twister (mt19937ar-cok)
   Initializing Mersenne Twister PRNG
   Random data written to Mersenne_Twister_mt19937ar-cok.bin.
   Initializing PRNG: ISAAC (rand.c 20010626)
   Initializing ISAAC PRNG
   Random data written to ISAAC_rand_c_20010626.bin.
   ...
   
   Benchmark Results for PRNGs (generated 500 MB):
   PRNG                                     : Seconds    : MB/s     
   ---------------------------------------------------------------------
   Mersenne Twister (mt19937ar-cok)         : 1.750000   : 285.71    
   ISAAC (rand.c 20010626)                  : 2.000000   : 250.00    
   ISAAC-64 (isaac64.c)                     : 2.250000   : 222.22    
   Lagged Fibonacci Generator               : 1.500000   : 333.33    
   XORoshiro-256                            : 1.250000   : 400.00    
   AES-256-CTR (OpenSSL)                    : 2.500000   : 200.00    
    
   The fastest PRNG is: XORoshiro-256 with 1.250000 seconds (400.00 MB/s).
   ```

4. **Custom Benchmark Size with Memory-Only Mode (e.g., 500 MB):**

   ```bash
   ./prng_benchmark --memonly --size 500
   ```

   **Output:**
   ```
   Benchmarking in memory-only mode. Random data will not be written to disk.
   
   Initializing PRNG: Mersenne Twister (mt19937ar-cok)
   Initializing Mersenne Twister PRNG
   Initializing PRNG: ISAAC (rand.c 20010626)
   Initializing ISAAC PRNG
   ...
   
   Benchmark Results for PRNGs (generated 500 MB):
   PRNG                                     : Seconds    : MB/s     
   ---------------------------------------------------------------------
   Mersenne Twister (mt19937ar-cok)         : 1.750000   : 285.71    
   ISAAC (rand.c 20010626)                  : 2.000000   : 250.00    
   ISAAC-64 (isaac64.c)                     : 2.250000   : 222.22    
   Lagged Fibonacci Generator               : 1.500000   : 333.33    
   XORoshiro-256                            : 1.250000   : 400.00    
   AES-256-CTR (OpenSSL)                    : 2.500000   : 200.00    
    
   The fastest PRNG is: XORoshiro-256 with 1.250000 seconds (400.00 MB/s).
   ```

### Help

Display help information:

```bash
./prng_benchmark --help
```

**Output:**

```
Usage: ./prng_benchmark [OPTIONS]

Options:
  --memonly        Run the benchmark in memory-only mode. Random data will not be written to disk.
  --size <MB>      Specify the size of data to generate in megabytes (MB). Default is 100 MB.
  -h, --help       Display this help message.
```

## Benchmark Output

After running the benchmark, the program outputs a table summarizing the performance of each PRNG. The table includes:

- **PRNG:** Name of the pseudorandom number generator.
- **Seconds:** Time taken to generate the specified amount of data.
- **MB/s:** Throughput, calculated as megabytes per second.

Additionally, the program identifies and announces the fastest PRNG based on the benchmark results.

**Sample Output:**

```
Benchmarking in memory-only mode. Random data will not be written to disk.

Initializing PRNG: Mersenne Twister (mt19937ar-cok)
Initializing Mersenne Twister PRNG
Initializing PRNG: ISAAC (rand.c 20010626)
Initializing ISAAC PRNG
Initializing PRNG: ISAAC-64 (isaac64.c)
Initializing ISAAC-64 PRNG
Initializing PRNG: Lagged Fibonacci Generator
Initializing Lagged Fibonacci Generator PRNG
Initializing PRNG: XORoshiro-256
Initializing XORoshiro-256 PRNG
Initializing PRNG: AES-256-CTR (OpenSSL)
Initializing AES-256-CTR PRNG

Benchmark Results for PRNGs (generated 100 MB):
PRNG                                     : Seconds    : MB/s     
---------------------------------------------------------------------
Mersenne Twister (mt19937ar-cok)         : 0.350000   : 285.71    
ISAAC (rand.c 20010626)                  : 0.400000   : 250.00    
ISAAC-64 (isaac64.c)                     : 0.450000   : 222.22    
Lagged Fibonacci Generator               : 0.300000   : 333.33    
XORoshiro-256                            : 0.250000   : 400.00    
AES-256-CTR (OpenSSL)                    : 0.500000   : 200.00    

The fastest PRNG is: XORoshiro-256 with 0.250000 seconds (400.00 MB/s).
```

**Explanation:**

- **PRNG:** Lists the name of each pseudorandom number generator tested.
  
- **Seconds:** Indicates the time taken to generate the specified amount of data.
  
- **MB/s:** Shows the throughput, calculated as megabytes generated per second.

The final line identifies the fastest PRNG based on the benchmarking results.

## License

This project is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html).

```
PRNG Benchmark is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

PRNG Benchmark is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with PRNG Benchmark. If not, see <https://www.gnu.org/licenses/>.
```

## Contributing

Contributions are welcome! Whether you're looking to report bugs, suggest features, or submit pull requests, your input is invaluable to improving the **PRNG Benchmark** tool.

### Steps to Contribute

1. **Fork the Repository:**

   Click the "Fork" button at the top right of this page to create a personal copy of the repository.

2. **Clone Your Fork:**

   ```bash
   git clone https://github.com/yourusername/prng-benchmark.git
   cd prng-benchmark
   ```

3. **Create a New Branch:**

   It's best practice to create a new branch for each feature or bug fix.

   ```bash
   git checkout -b feature/your-feature-name
   ```

4. **Make Your Changes:**

   Implement your feature or fix the bug in the relevant files.

5. **Commit Your Changes:**

   Write clear and descriptive commit messages.

   ```bash
   git commit -m "Add feature: your feature description"
   ```

6. **Push to Your Fork:**

   ```bash
   git push origin feature/your-feature-name
   ```

7. **Submit a Pull Request:**

   Go to the original repository on GitHub and click the "Compare & pull request" button. Provide a detailed description of your changes and why they are beneficial.

### Guidelines

- **Code Quality:** Ensure your code adheres to the project's coding standards and passes any existing tests.

- **Documentation:** Update the `README.md` and other documentation as necessary to reflect your changes.

- **Testing:** Include tests for new features or verify that existing tests pass.

- **Commit Messages:** Use clear and concise commit messages that accurately describe the changes.

## Acknowledgements

- **OpenSSL:** Utilized for the AES-256-CTR PRNG implementation.
  
- **PRNG Developers:** Special thanks to the developers of the various PRNG algorithms integrated into this benchmark tool.

- **Community Contributors:** Appreciation to all contributors who have helped improve this project through feedback, bug reports, and code contributions.

