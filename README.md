# nwipe

![GitHub CI badge](https://github.com/martijnvanbrummelen/nwipe/workflows/ci_ubuntu_latest/badge.svg)
[![GitHub release](https://img.shields.io/github/release/martijnvanbrummelen/nwipe)](https://github.com/martijnvanbrummelen/nwipe/releases/)

**nwipe** is a command-line utility designed to securely erase disks. It is a fork of the `dwipe` command used by Darik's Boot and Nuke ([DBAN](https://dban.org/)), created to allow disk wiping on any host distribution for better hardware support. nwipe can erase single or multiple disks simultaneously, either through a command-line interface or a user-friendly ncurses GUI.

> **Warning**
> For some features, such as SMART data in the PDF certificate and HPA/DCO detection, nwipe utilizes `smartmontools` and `hdparm`. Both are mandatory requirements for full functionality. Without them, nwipe will display warnings and some features may not work as intended.

![Example wipe](https://raw.githubusercontent.com/martijnvanbrummelen/nwipe/master/images/example_wipe.gif)

*The video above shows six drives being simultaneously erased. It skips to the completion of all six wipes, showing five drives successfully erased and one failed due to an I/O error. The failed drive would typically be physically destroyed. The successfully wiped drives can be redeployed.*

![nwipe_certificate](https://github.com/martijnvanbrummelen/nwipe/assets/22084881/cf181a9c-af2d-4bca-a6ed-15a4726cb12b)

*The snapshot above shows nwipe's three-page PDF certificate. Drive identifiable information like serial numbers has been anonymized using the `-q` option.*

## Table of Contents

- [Features](#features)
- [Erasure Methods](#erasure-methods)
- [Limitations with Solid State Drives](#limitations-with-solid-state-drives)
- [Installation](#installation)
  - [Prerequisites](#prerequisites)
    - [Debian & Ubuntu](#debian--ubuntu)
    - [Fedora](#fedora)
  - [Compilation](#compilation)
- [Usage](#usage)
- [Development](#development)
- [Automated Installation Script](#automated-installation-script)
- [Bootable Version](#bootable-version)
- [Distributions Including nwipe](#distributions-including-nwipe)
- [Reporting Bugs](#reporting-bugs)
- [License](#license)

## Features

- Securely erase entire disks, single or multiple simultaneously.
- Supports various recognized secure erase methods.
- Operates via command-line interface or ncurses GUI.
- Generates a detailed three-page PDF certificate documenting the erase process.
- Detects and handles Host Protected Area (HPA) and Device Configuration Overlay (DCO).
- Retrieves SMART data for drives.

## Erasure Methods

Users can select from a variety of recognized secure erase methods:

- **Fill With Zeros**: Fills the device with zeros (`0x00`), one pass.
- **Fill With Ones**: Fills the device with ones (`0xFF`), one pass.
- **RCMP TSSIT OPS-II**: Royal Canadian Mounted Police standard, OPS-II.
- **DoD Short**: U.S. Department of Defense 5220.22-M short 3-pass wipe (passes 1, 2 & 7).
- **DoD 5220.22-M**: U.S. Department of Defense 5220.22-M full 7-pass wipe.
- **Gutmann Wipe**: Peter Gutmann's method for secure deletion.
- **PRNG Stream**: Fills the device with data from a Pseudo-Random Number Generator.
- **Verify Zeros**: Reads the device to verify it's filled with zeros.
- **Verify Ones**: Reads the device to verify it's filled with ones.
- **HMG IS5 Enhanced**: Secure sanitization for sensitive information.

### Pseudo-Random Number Generators (PRNGs)

- **Mersenne Twister**
- **ISAAC**
- **XORoshiro-256** (Available in v0.37 and later)
- **Lagged Fibonacci** (Available in v0.37 and later)
- **AES-CTR (OpenSSL)** (Available in v0.37 and later)

These PRNGs can be used to overwrite a drive with randomly generated data.

## Limitations with Solid State Drives

nwipe does not fully sanitize Solid State Drives (SSDs) due to their architecture, which includes overprovisioning and wear-leveling mechanisms that reserve parts of the memory inaccessible to the host system. It's recommended to use nwipe alongside the manufacturer's or hardware vendor's tools for SSD sanitization to ensure complete data destruction. For more information and tools for SSD wipes, refer to the [SSD Guide](ssd-guide.md).

## Installation

### Prerequisites

#### Debian & Ubuntu

Before compiling nwipe from source, install the following dependencies:

```bash
sudo apt update
sudo apt install \
  build-essential \
  pkg-config \
  automake \
  libncurses5-dev \
  autotools-dev \
  libparted-dev \
  libconfig-dev \
  libconfig++-dev \
  hdparm \
  smartmontools \
  dmidecode \
  coreutils \
  git
```

#### Fedora

```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install \
  ncurses-devel \
  parted-devel \
  libconfig-devel \
  libconfig++-devel \
  hdparm \
  smartmontools \
  dmidecode \
  coreutils \
  git
```

### Compilation

First, create all the autoconf files:

```bash
./autogen.sh
```

Then compile and install:

```bash
./configure
make
sudo make install
```

Run nwipe:

```bash
sudo nwipe
```

## Usage

To start nwipe with the ncurses GUI:

```bash
sudo nwipe
```

For command-line options:

```bash
nwipe --help
```

## Development

If you wish to contribute to nwipe, please enable all warnings when compiling:

```bash
./configure --prefix=/usr CFLAGS='-O0 -g -Wall -Wextra'
make format  # Necessary if submitting pull requests
make
sudo make install
```

- The `-O0 -g` flags disable optimizations for debugging.
- The `-Wall` and `-Wextra` flags enable all compiler warnings.
- Use `make format` to ensure code style consistency as defined in the `.clang-format` file (requires `clang-format`).

After development, you can compile with optimizations:

```bash
./configure --prefix=/usr
make
sudo make install
```

## Automated Installation Script

For Debian-based distributions, automate the download and compilation with the following script:

```bash
#!/bin/bash
cd "$HOME"
nwipe_directory="nwipe_master"
mkdir -p $nwipe_directory
cd $nwipe_directory
sudo apt update
sudo apt install -y \
  build-essential \
  pkg-config \
  automake \
  libncurses5-dev \
  autotools-dev \
  libparted-dev \
  libconfig-dev \
  libconfig++-dev \
  hdparm \
  smartmontools \
  dmidecode \
  coreutils \
  git
rm -rf nwipe
git clone https://github.com/martijnvanbrummelen/nwipe.git
cd nwipe
./autogen.sh
./configure
make
cd src
sudo ./nwipe
```

Save this script as `buildnwipe.sh`, give it execute permissions with `chmod +x buildnwipe.sh`, and run it to compile and start nwipe.

## Bootable Version

To try a bootable version of nwipe, download [ShredOS](https://github.com/PartialVolume/shredos.x86_64). ShredOS is a small (~60MB) bootable image containing the latest nwipe version. It supports both BIOS and UEFI boot modes and can be written to a USB flash drive or CD/DVD.

Instructions and downloads are available [here](https://github.com/PartialVolume/shredos.x86_64#obtaining-and-writing-shredos-to-a-usb-flash-drive-the-easy-way-).

## Distributions Including nwipe

nwipe is included in many Linux distributions. For the latest version availability, see [Repology](https://repology.org/project/nwipe/versions).

Distributions that include nwipe:

- [ShredOS](https://github.com/PartialVolume/shredos.x86_64) - Always has the latest nwipe release.
- [netboot.xyz](https://github.com/netbootxyz/netboot.xyz) - Can network-boot ShredOS.
- [DiskDump](https://github.com/Awire9966/DiskDump) - nwipe on Debian live CD, can wipe eMMC chips.
- [Parted Magic](https://partedmagic.com)
- [SystemRescue](https://www.system-rescue.org)
- [GParted Live](https://gparted.org/livecd.php)
- [Grml](https://grml.org/)

If you know of other distributions that include nwipe, please let us know or submit a pull request to update this README.

## Reporting Bugs

Report bugs on GitHub: [nwipe Issues](https://github.com/martijnvanbrummelen/nwipe/issues)

## License

This project is licensed under the **GNU General Public License v3.0**. See the [LICENSE](LICENSE) file for details.
