# nwipe
![GitHub CI badge](https://github.com/martijnvanbrummelen/nwipe/workflows/ci_ubuntu_latest/badge.svg)
![GitHub CI badge](https://github.com/martijnvanbrummelen/nwipe/workflows/ci_ubuntu_16.04/badge.svg)
[![GitHub release](https://img.shields.io/github/release/martijnvanbrummelen/nwipe)](https://github.com/martijnvanbrummelen/nwipe/releases/)

nwipe is a fork of the dwipe command originally used by Darik's Boot and Nuke (DBAN). nwipe was created out of a need to run the DBAN dwipe command outside of DBAN, in order to allow its use with any host distribution, thus giving better hardware support.

nwipe is a program that will securely erase the entire contents of disks. It can wipe a single drive or multiple disks simultaneously. It can operate as both a command line tool without a GUI or with a ncurses GUI as shown in the example below:

> **Warning**
>  Nwipe does not currently support HDA (hidden disc area) detection but will be available in the next release v0.35. For versions prior to this, you will need to run hdparm to detect and if necessary correct the reported size of the disc prior to using nwipe. `sudo hdparm -N /dev/sdx` and `sudo hdparm --dco-identify /dev/sdx`. The three sector values returned should all be identical for there to be no hidden sectors on the disc. xxxxxxxxxx / yyyyyyyyyy as returned by hdparm -N and Real max sectors = zzzzzzzzzz as returned by hdparm --dco-identify.

![Example wipe](/images/example_wipe.gif)

<i>GIF showing six drives being simultaneously erased. It skips to the completion of all six wipes and shows five drives that were successfully erased and one drive that failed due to an I/O error. The drive that failed would then normally be physically destroyed. The five drives that were successfully wiped with zero errors or failures can then be redeployed.</i>

The user can select from a variety of recognised secure erase methods which include:

* Fill With Zeros    - Fills the device with zeros (0x00), one round only.
* Fill With Ones     - Fills the device with ones  (0xFF), one round only.
* RCMP TSSIT OPS-II  - Royal Candian Mounted Police Technical Security Standard, OPS-II
* DoD Short          - The American Department of Defense 5220.22-M short 3 pass wipe (passes 1, 2 & 7).
* DoD 5220.22M       - The American Department of Defense 5220.22-M full 7 pass wipe.
* Gutmann Wipe       - Peter Gutmann's method (Secure Deletion of Data from Magnetic and Solid-State Memory).
* PRNG Stream        - Fills the device with a stream from the PRNG.
* Verify Zeros       - This method only reads the device and checks that it is filled with zeros (0x00).
* Verify Ones        - This method only reads the device and checks that it is filled with ones (0xFF).
* HMG IS5 enhanced   - Secure Sanitisation of Protectively Marked Information or Sensitive Information

nwipe also includes the following pseudo random number generators:
* Mersenne Twister
* ISAAC

These can be used to overwrite a drive with a stream of randomly generated characters.

nwipe can be found in many [Linux distro repositories](#which-linux-distro-uses-the-latest-nwipe).

nwipe is also included in [ShredOS](https://github.com/PartialVolume/shredos.x86_64) which was developed in particular to showcase nwipe as a fast-to-boot standalone method similar to DBAN. ShredOS always contains the latest nwipe version.

## Compiling & Installing

For a development setup, see the [Hacking section](#hacking) below. For a bootable version of the very latest nwipe master that you can write to an USB flash drive or CD/DVD, see the [Quick and easy bootable version of nwipe master section](#quick--easy-usb-bootable-version-of-nwipe-master-for-x86_64-systems) below.

`nwipe` requires the following libraries to be installed:

* ncurses
* pthreads
* parted
* libconfig

`nwipe` also requires the following program to be installed, it will abort with a warning if not found:

* hdparm (as of current master and v0.35+)

and optionally, but recommended, the following programs:

* dmidecode
* readlink
* smartmontools

### Debian & Ubuntu prerequisites

If you are compiling `nwipe` from source, the following libraries will need to be installed first:

```bash
sudo apt install \
  build-essential \
  pkg-config \
  automake \
  libncurses5-dev \
  autotools-dev \
  libparted-dev \
  libconfig-dev \
  libconfig++-dev \
  dmidecode \
  coreutils \
  smartmontools \
  hdparm
```

### Fedora prerequisites

```bash
sudo bash
dnf update
dnf groupinstall "Development Tools"
dnf groupinstall "C Development Tools and Libraries"
yum install ncurses-devel
yum install parted-devel
yum install libconfig-devel
yum install libconfig++-devel
yum install dmidecode
yum install coreutils
yum install smartmontools
yum install hdparm
```
Note. The following programs are optionally installed although recommended. 1. dmidecode 2. readlink 3. smartmontools.

#### hdparm [REQUIRED]
hdparm provides some of the information regarding disk size in sectors as related to the host protected area (HPA) and device configuration overlay (DCO). We do however have our own function that directly access the DCO to obtain the 'real max sectors' so reliance on hdparm may be removed at a future date.

#### dmidecode [RECOMMENDED]
dmidecode provides SMBIOS/DMI host data to stdout or the log file. If you don't install it you won't see the SMBIOS/DMI host data at the beginning of nwipes log.

#### coreutils (provides readlink) [RECOMMENDED]
readlink determines the bus type, i.e. ATA, USB etc. Without it the --nousb option won't work and bus type information will be missing from nwipes selection and wipe windows. The coreutils package is often automatically installed as default in most if not all distros.

#### smartmontools [RECOMMENDED]
smartmontools obtains serial number information for supported USB to IDE/SATA adapters. Without it, drives plugged into USB ports will not show serial number information.

If you want a quick and easy way to keep your copy of nwipe running the latest master release of nwipe see the [automating the download and compilation](#automating-the-download-and-compilation-process-for-debian-based-distros) section.

### Compilation

First create all the autoconf files:
```
./autogen.sh
```

Then compile & install using the following standard commands:
```
./configure
make format (only required if submitting pull requests)
make
make install
```

Then run nwipe !
```
cd src
sudo ./nwipe
```
or
```
sudo nwipe
```

### Hacking

If you wish to submit pull requests to this code we would prefer you enable all warnings when compiling.
This can be done using the following compile commands:

```
./configure --prefix=/usr CFLAGS='-O0 -g -Wall -Wextra'
make format (necessary if submitting pull requests)
make
make install
```

The `-O0 -g` flags disable optimisations. This is required if you're debugging with `gdb` in an IDE such as Kdevelop. With these optimisations enabled you won't be able to see the values of many variables in nwipe, not to mention the IDE won't step through the code properly.

The `-Wall` and `-Wextra` flags enable all compiler warnings. Please submit code with zero warnings.

Also make sure that your changes are consistent with the coding style defined in the `.clang-format` file, using:
```
make format
```
You will need `clang-format` installed to use the `format` command.

Once done with your coding then the released/patch/fixed code can be compiled, with all the normal optimisations, using:
```
./configure --prefix=/usr && make && make install
```

## Automating the download and compilation process for Debian based distros.

Here's a script that will do just that! It will create a directory in your home folder called 'nwipe_master'. It installs all the libraries required to compile the software (build-essential) and all the libraries that nwipe requires (libparted etc). It downloads the latest master copy of nwipe from github. It then compiles the software and then runs the latest nwipe. It doesn't write over the version of nwipe that's installed in the repository (If you had nwipe already installed). To run the latest master version of nwipe manually you would run it like this `sudo ~/nwipe_master/nwipe/src/nwipe`

You can run the script multiple times; the first time it's run it will install all the libraries; subsequent times it will just say the libraries are up to date. As it always downloads a fresh copy of the nwipe master from Github, you can always stay up to date. Just run it to get the latest version of nwipe. It only takes 11 seconds to compile on my i7.

If you already have nwipe installed from the repository, you need to take care which version you are running. If you typed `nwipe` from any directory it will always run the original repository copy of nwipe. To run the latest nwipe you have to explicitly tell it where the new copy is, e.g in the directory `~/nwipe_master/nwipe/src`. That's why you would run it by typing `sudo ~/nwipe_master/nwipe/src/nwipe` alternatively you could cd to the directory and run it like this:

```
cd ~/nwipe_master/nwipe/src
./nwipe
```

Note the ./, that means only look in the current directory for nwipe. if you forgot to type ./ the computer would run the older repository version of nwipe.

Once you have copied the script below into a file called buildnwipe, you need to give the file execute permissions `chmod +x buildnwipe` before you can run it. [Download script](
https://drive.google.com/file/d/1BsQDlMqtEycAgfk9FpG1sxv3jFz5dzNO/view?usp=sharing)
```
#!/bin/bash
cd "$HOME"
nwipe_directory="nwipe_master"
mkdir $nwipe_directory
cd $nwipe_directory
sudo apt install build-essential pkg-config automake libncurses5-dev autotools-dev libparted-dev dmidecode readlink smartmontools git
rm -rf nwipe
git clone https://github.com/martijnvanbrummelen/nwipe.git
cd "nwipe"
./autogen.sh
./configure
make
cd "src"
sudo ./nwipe
```

## Quick & Easy, USB bootable version of nwipe master for x86_64 systems.
If you want to just try out a bootable version of nwipe you can download [ShredOS](https://github.com/PartialVolume/shredos.x86_64). The ShredOS image is around 60MB and can be written to an USB flash drive in seconds, allowing you to boot straight into the latest version of nwipe. ShredOS is available for x86_64 (64-bit) and i686 (32-bit) CPU architectures and will boot both legacy BIOS and UEFI devices. It comes as .IMG (bootable USB flash drive image) or .ISO (for CD-R/DVD-R). Instructions and download can be found [here](https://github.com/PartialVolume/shredos.x86_64#obtaining-and-writing-shredos-to-a-usb-flash-drive-the-easy-way-).

## Which Linux distro uses the latest nwipe?
See [Repology](https://repology.org/project/nwipe/versions)

And in addition checkout the following distros that all include nwipe:

- [ShredOS](https://github.com/PartialVolume/shredos.x86_64) Always has the latest nwipe release.
- [netboot.xyz](https://github.com/netbootxyz/netboot.xyz) Can network-boot ShredOS.
- [partedmagic](https://partedmagic.com)
- [SystemRescueCD](https://www.system-rescue.org)
- [gparted live](https://sourceforge.net/projects/gparted/files/gparted-live-testing/1.2.0-2/)
- [grml](https://grml.org/)

Know of other distros that include nwipe? Then please let us know or issue a PR on this README.md. Thanks.

## Bugs

Bugs can be reported on GitHub:
https://github.com/martijnvanbrummelen/nwipe

## License

GNU General Public License v2.0
