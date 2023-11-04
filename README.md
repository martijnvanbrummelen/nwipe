# nwipe
![GitHub CI badge](https://github.com/martijnvanbrummelen/nwipe/workflows/ci_ubuntu_latest/badge.svg)
[![GitHub release](https://img.shields.io/github/release/martijnvanbrummelen/nwipe)](https://github.com/martijnvanbrummelen/nwipe/releases/)

nwipe is a fork of the dwipe command originally used by Darik's Boot and Nuke (DBAN). nwipe was created out of a need to run the DBAN dwipe command outside of DBAN, in order to allow its use with any host distribution, thus giving better hardware support.

nwipe is a program that will securely erase the entire contents of disks. It can wipe a single drive or multiple disks simultaneously. It can operate as both a command line tool without a GUI or with a ncurses GUI as shown in the example below:

> **Warning**
> For some of nwipes features such as smart data in the PDF certificate, HPA/DCO detection and other uses, nwipe utilises smartmontools and hdparm. Therefore both hdparm & smartmontools are a mandatory requirement if you want all of nwipes features to be fully available. If you do not install smartmontools and hdparm, nwipe will provide a warning in the log that these programs cannot be found but will still run but many important features may not work as they should do.

# New Features in 0.35
- Nwipe now supports HPA/DCO (hidden disc area) detection. The status of a discs HPA/DCO is shown on nwipes's drive selection screen. This allows you to identify a drive that is reporting a smaller size than it actually is. To wipe the hidden sectors the HPA/DCO must be reset to the true size of the disc. Currently nwipe cannot reset the HPA/DCO (scheduled for 0.36). To reset the HPA/DCO you would need to manually perform a reset using the command line tool hdparm.
- Nwipe now optionally generates a multi page PDF certificate for each drive erased. Page one of the PDF is a erasure certificate that shows the erasure details such as drive make, model, serial number, bytes erased, HPA/DCO status, organisation performing the erase, customer details, technician name, status of erasure, errors detected.
- Nwipe now has a configuration file, /etc/nwipe/nwipe.conf. This is currently used to store PDF parameters but will be enhanced over the next versions to include default wipe parameters amongst other additions.
- There is a new configuration screen in nwipe. It can be accessed from the drive selection screen by typing 'c'. Curerntly the configuration screen allows changes to be made to the PDF certificate however later versions will expand this configuration screen.
- Nwipe now provides better support for temperature retrieval specifically on SAS hardware, so that both SATA and SAS drives should now all show temperatures both at drive selection and during the erasure.
- Nwipes temperature retrieval code has been placed in it's own thread. This was done because it was found that any delays in obtaining the temperature resulted in a momentary freeze in the GUI wipe screen updating it's stats. This wasn't noticable if you were erasing a small number of drives but become apparent when wiping ten or twenty drives simultaneously.

![Example wipe](/images/example_wipe.gif)

<i>The video above shows six drives being simultaneously erased. It skips to the completion of all six wipes and shows five drives that were successfully erased and one drive that failed due to an I/O error. The drive that failed would then normally be physically destroyed. The five drives that were successfully wiped with zero errors or failures can then be redeployed.</i>

![nwipe_certificate_0 35_5s](https://github.com/martijnvanbrummelen/nwipe/assets/22084881/cf181a9c-af2d-4bca-a6ed-15a4726cb12b)
<i>The snapshot above shows nwipe's three page PDF certificate, drive identifiable information such as serial numbers have been anonymised using the nwipe command line option -q</i>

## Erasure methods
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

#### smartmontools [REQUIRED]
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

Once you have copied the script below into a file called buildnwipe, you need to give the file execute permissions `chmod +x buildnwipe` before you can run it.
```
#!/bin/bash
cd "$HOME"
nwipe_directory="nwipe_master"
mkdir $nwipe_directory
cd $nwipe_directory
sudo apt install build-essential pkg-config automake libncurses5-dev autotools-dev libparted-dev libconfig-dev libconfig++-dev dmidecode readlink smartmontools git
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
