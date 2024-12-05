# XORoshiro-256 PRNG by Fabian Druschke

## Overview

This repository contains the implementation of **XORoshiro-256**, a novel pseudorandom number generator (PRNG) developed and implemented by **Fabian Druschke**. This algorithm is based on the widely known XORoshiro-128** family of PRNGs but has been extended to use a 256-bit internal state to deliver higher throughput and randomness quality.

This implementation is specifically designed for **nwipe**, a secure data wiping tool, and has been optimized to deliver fast and high-quality random data generation on **64-bit systems**. The focus of this PRNG is to provide rapid throughput without compromising on randomness quality for non-cryptographic applications.

## Key Differences from XORoshiro-128**

The **XORoshiro-256** implementation retains the core principles of the XORoshiro-128** algorithm but introduces several key modifications to improve its performance and applicability in specific use cases:

- **Expanded State Size**: Unlike XORoshiro-128**, which uses a 128-bit internal state, **XORoshiro-256** utilizes a 256-bit state. This change increases the entropy potential and allows the generator to handle a larger amount of randomness, making it more resilient to state collisions over longer sequences.
  
- **Full State Output**: In XORoshiro-128**, the output is derived by manipulating part of the internal state. In **XORoshiro-256**, the entire 256-bit internal state is written directly into the output buffer after each iteration. This ensures that the full state is leveraged for output, maximizing the randomness per cycle.

- **Customized Rotation and Shift Operations**: The core operations (XOR, rotate, shift) have been carefully tuned in **XORoshiro-256** to maintain high entropy propagation throughout the internal state. The rotations and shifts are specifically designed to distribute randomness across the full state efficiently.

- **Optimized for nwipe**: This PRNG has been tailored for **nwipe**, where fast, secure, and high-throughput random number generation is essential for secure data wiping processes. **XORoshiro-256** is optimized to run efficiently on **64-bit systems**, delivering high performance and quality randomness.

- **Non-cryptographic**: As with XORoshiro-128**, **XORoshiro-256** is not suitable for cryptographic purposes. While it provides high-quality randomness for general applications, it is predictable and lacks the security features needed for cryptographic use.

## Disclaimer

The **XORoshiro-256 PRNG** has been developed following **best practices**, with a focus on performance and randomness quality. However, **Fabian Druschke** makes **no warranties** regarding the suitability of this algorithm for specific use cases. This PRNG is **not intended for cryptographic purposes**, and users should exercise caution when using it in sensitive environments.

By using this software, you agree that **Fabian Druschke** is not responsible for any potential issues, damages, or data loss resulting from its use. The software is provided "as-is" and is used at your own risk.

## License

This implementation is released into the **public domain**. You are free to use, modify, and distribute this software for any purpose, without restriction. Please note that there are **no guarantees** of the quality of the randomness generated or its fitness for any particular purpose.

## Use Case

This PRNG has been designed to meet the specific needs of **nwipe**, a data wiping tool where fast and secure random data generation is crucial. It is optimized for **64-bit architectures** and ensures a high level of performance when generating large amounts of random data quickly.

## Mathematical and Statistical Analysis

The **entropy** and **randomness properties** of the XORoshiro-256 PRNG have been rigorously tested. The algorithm's **entropy propagation** has been proven mathematically, and it has been subjected to a variety of statistical tests, including **Diehard** and **TestU01**, to evaluate its randomness quality.

For more detailed information on the mathematical proof and statistical analysis, please refer to the [paper](./XOROSHIRO_PROOF_OF_CONCEPT.md).

## Contributions

Contributions to this project are welcome! If you have suggestions for improvements or want to collaborate, feel free to open an issue or submit a pull request.

