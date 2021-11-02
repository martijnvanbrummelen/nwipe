RELEASE NOTES
=============

v0.32
-----------------------
- Add ones (0xFF) wipe to the methods. Renamed Zero Fill to Fill with Zeros and the new ones wipe, is called Fill with Ones.
- Add ones verication to the methods. Renamed Verify Blank to Verify Zeros (0x00) and the new verification is called Verify Ones (0xFF).
- Move method information from below the list of methods to the right of the method list. This allows better use of the screen space by allowing more methods to be added to the list, especially relevant to nwipe running as a standalone application on small distros such as shredos 2020 in frame buffer mode.
- Removed the old DBAN syslinux.cfg configuration hints as not relevant to nwipe. See nwipe --help or man nwipe for command line options.
- Add fdatasync errors to the error summary table.
- During a wipe, you can now toggle between dark screen, blank screen and default blue screen by repeatedly pressing the b key. Dark screen, which is grey text on black background has been introduced to prevent TFT/LCD image persistence on monitors that are sensitive to that issue.

v0.31
-----------------------
- Blanking disabled in GUI for OPS2 (mandatory requirement of standard). [#326](https://github.com/martijnvanbrummelen/nwipe/pull/326)
- Total bytes written/read for ALL passes or verifications are now logged. [#326](https://github.com/martijnvanbrummelen/nwipe/pull/326)
- Final blanking being enabled is no longer required for verification passes. GUI Fix. [#326](https://github.com/martijnvanbrummelen/nwipe/pull/326)
- Add a summary table to the log that shows totals for pass & verification errors. [#325](https://github.com/martijnvanbrummelen/nwipe/pull/325)
- Fix the missing 'Verifying' message on final blanking. [#324](https://github.com/martijnvanbrummelen/nwipe/pull/324)
- Fix prng selection always using mersenne irrespective of whatever prng the user selected. [#323](https://github.com/martijnvanbrummelen/nwipe/pull/323)
- Fix a non functional Isaac prng. (May have never worked even in DBAN/dwipe 2.3.0). [#322](https://github.com/martijnvanbrummelen/nwipe/pull/322)
- Log whether the prng produces a stream, if not log failure message. [#321](https://github.com/martijnvanbrummelen/nwipe/pull/321)
- Log the specific prng that is initialised. [#320](https://github.com/martijnvanbrummelen/nwipe/pull/320)
- Log selection details to the log. [#319](https://github.com/martijnvanbrummelen/nwipe/pull/319)
- Improve log messaging. [#317](https://github.com/martijnvanbrummelen/nwipe/pull/317)
- Fix auto shutdown option for some distros. [#315](https://github.com/martijnvanbrummelen/nwipe/pull/315)
- Fix build for musl. [#301](https://github.com/martijnvanbrummelen/nwipe/pull/301)
- Fixes to summary table & fix final status message. [311](https://github.com/martijnvanbrummelen/nwipe/pull/311/commits/12063ad9549860cd625cb91d047bd304217a9ebf)
- Updates to --help options [#309](https://github.com/martijnvanbrummelen/nwipe/pull/309/commits/69c9d7d6c5a091c58b3e747078d0022ccdd95a99)
- Updates to manpage. [#300](https://github.com/martijnvanbrummelen/nwipe/commit/7cc1a68a89236c4b501dde9149be82c208defccd)

v0.30
-----------------------
- Add auto power off option on completion of wipe ( --autopoweroff ) (Thanks PartialVolume)
- Fixed --nowait option that wasn't working. (Thanks PartialVolume)
- Add verbose option. -v, --verbose.
- Add a spinner to the GUI for each drive being wiped. When nwipe is syncing the percentage completion pauses, having a spinner gives a clear indication that the wipe is still running. Each devices spinner disappears on completion of a given devices wipe. (Thanks PartialVolume)
- Make log messages, especially the ones with the tag 'notice' succinct and less than 80 characters including the timestamp. This is of more importance when nwipe is used on a 80x30 terminal (ALT-F2, Shredos etc) but generally makes the logs more readable. While doing this all information was still retained. (Thanks PartialVolume)
- Add a summary table to the log that shows each drives status, i.e. erased or failed, throughput, duration of wipe, model, serial no etc. In particular it benefits those that wipe many drives simultaneously in rack servers. At a glance any failed drives can be seen without having to browse back through the log. (Thanks PartialVolume)
- Add ETA to --nogui wipes status when SIGUSR1 (kill -s USR1 (nwipes PID) is issued on the command line.
- Fixed misleading throughput calculation. Throughput now shows average throughput calculated from start of wipe.
- Fixed system info not being displayed in Debian Sid. [#229](https://github.com/martijnvanbrummelen/nwipe/issues/229) (Thanks PartialVolume)
- Add serial number display for USB to IDE/SATA adapters. This only works if the USB to IDE/SATA adapter supports ATA pass through. See [#149](https://github.com/martijnvanbrummelen/nwipe/issues/149) for further details (Thanks PartialVolume)
- Fixed disk capacity nomenclature, width and padding on drive selection screen. See [#237](https://github.com/martijnvanbrummelen/nwipe/issues/237) (Thanks PartialVolume)
- Add bus type, ATA or USB, amongst others to drive selection and wipe windows. (Thanks PartialVolume)
- Add --nousb option. If you use the option --nousb, all USB devices will be ignored. They won't show up in the GUI and they won't be wiped if you use the --nogui --autonuke command. They will even be ignored if you specifically name them on the command line.
- Miscellaneous GUI fixes, throughput display format, percentage display format to improve column alignment when wiping multiple discs. (Thanks PartialVolume)
- Improve visibility of failure messages with red text on white background. (Thanks PartialVolume)
- Add NVME and VIRT (loop etc) devices to device type table for display in GUI and logs. NVME devices now show up as NVME devices rather than UNK (Thanks PartialVolume)
- Fixed very obscure segmentation fault going back to at least 0.24 in drive selection window when resizing terminal vertical axis while drive focus symbol '>' is pointing to the last drive of a multi drive selection window. See [#248](https://github.com/martijnvanbrummelen/nwipe/pull/248) for further details (Thanks PartialVolume)
- Warn the user if they are incorrectly typing a lower case s to start a wipe, when they should be typing a capital S [#262](https://github.com/martijnvanbrummelen/nwipe/issues/262) (Thanks PartialVolume)
- Warn the user if they are typing capital S in order to start a wipe but haven't yet selected any drives for wiping [#261](https://github.com/martijnvanbrummelen/nwipe/issues/261) (Thanks PartialVolume)
- Add ctrl A that toggles drive selection, all drives selected for wipe or all drives deselected. [#266](https://github.com/martijnvanbrummelen/nwipe/issues/266)
- Fixed compilation issue with NixOS with broken musl libc error due to missing header [#275](https://github.com/martijnvanbrummelen/nwipe/issues/275)
- Fixed status bar message showing incorrect information [#287](https://github.com/martijnvanbrummelen/nwipe/issues/287)
- Right Justify log labels to maintain column alignment [#280](https://github.com/martijnvanbrummelen/nwipe/issues/280)
- Added nwipe version & OS info to log [#297](https://github.com/martijnvanbrummelen/nwipe/pull/297)

v0.28
-----------------------
- Fixed premature exit when terminal resized on completion of wipes (Thanks PartialVolume)
- Fixed GUI when terminal is resized, currently not handled correctly causing missing or incorrectly sized ncurses windows/panels (Thanks PartialVolume)
- Fixed GUI screen flicker under various situations. [#200](https://github.com/martijnvanbrummelen/nwipe/pull/200) Fixes [#115](https://github.com/martijnvanbrummelen/nwipe/issues/115) (Thanks PartialVolume)
- Fixed responsivness of screen during wipe when resized. Info is updated every 10th/sec. Key presses are more responsive. (Thanks PartialVolume)
- Fixed compiler warning regarding buffer overflow. Fixes [#202](https://github.com/martijnvanbrummelen/nwipe/issues/202) (Thanks PartialVolume)
- Fixed Man page (Thanks martijnvanbrummelen)
- Fixed individual device throughput. On completion of a wipe instead of the throughput calculation stopping for a completed wipe, it would continue to calculate resulting in a particular drives throughtput slowly dropping until eventually it reached zero. The overall throughput was not affected. (Thanks PartialVolume)

v0.27
-----------------------
- Add `verify` method to verify a disk is zero filled [#128](https://github.com/martijnvanbrummelen/nwipe/pull/128) (Thanks Legogizmo)
- Add new HMG IS5 enhanced wipe method [#168](https://github.com/martijnvanbrummelen/nwipe/pull/168) (Thanks infrastation)
- Fixed percentage progress and show on completion of wipe (Thanks PartialVolume)
- Implement clang-format support (Thanks louib)
- Implement more frequent disk sync support (Thanks Legogizmo)
- Format command line help to 80 character line length [#114](https://github.com/martijnvanbrummelen/nwipe/pull/114) (Thanks PartialVolume)
- Fixed nwipe message log and missing messages that was causing segfaults under certain conditions (Thanks PartialVolume)
- Add the Github build CI service and update Readme with build status labels (Thanks louib)
- Miscellaneous smaller fixes


v0.26
-----
- Add exclude drive option (Thanks PartialVolume)
- Log hardware (Thanks PartialVolume)

v0.25
-----
- Correct J=Up K=Down in footer (Thanks PartialVolume)
- Fixed segfault initialize `nwipe_gui_thread` (Thanks PartialVolume)
- Fixed memory leaks (Thanks PartialVolume)
- Check right pointer (Thanks PartialVolume)
- Fixed casting problem (Thanks PartialVolume)
- Fixed serial number
- Fixed uninitialized variable warning (Thanks PartialVolume)

v0.24
-----
- use include values for version 0.17
- display throughput value more friendly (Thanks Kelderek)

v0.23
-----
- make serial visible again on 32Bit machines

v0.22
-----
- Update manpage
- use long long for device size
- Use `ped_unit_format_byte` function to display(friendly) size of device

v0.21
-----
- Fixed ETA not updating properly and bad total throughput display. (Thanks Niels Bassler).

v0.20
-----
- Fixed build when panel header is not in `/usr/include` (Thanks Vincent Untz).

v0.19
-----
- Fixed building on Fedora(Unknown `off64_t`) bug #19.
- Use PRNG instead of zero's bug #7. (Thanks xambroz).

v0.18
-----
- Fixed grammar.
- Move from `loff_t` to `off64_t` for musl libc support.
- Add `--nosignals` option.
- Automake needs the `dist_` prefix to include man pages in `make dist`.
- Remove more compiler warnings.
- Add libintl, libuuid dependencies to allow parted static link

v0.17
-----
- Remove control reaches end of non-void function" warnings (Thanks Vincent Untz).
- Remove unused variables (Thanks Vincent Untz).
- Change start key to 'S' instead of F10 (closes debian bug #755474).
- Fixed problem with unusable device (Closes debian bug #755473).

v0.16
-----
- Fixed problems building with clang compiler (Thanks Martijn van Brummelen)

v0.15
-----
- Add more detailed information to status page when wiping
- Add ability to send SIGUSR1 to print wiping current status to log
- Fixed problem with status bar disappearing on narrow windows (Github issue #1)

v0.14
-----
- Added explicit check for ncurses (required for Fedora). See bug 3604008.

v0.13
-----
- Added nowait option (patch 3601259 - thanks David Shaw).
- Added nogui option.
- Updated man page and help command for above options and autonuke.
- Added pkg-config check for ncurses (patch 3603140 - thanks Alon Bar-Lev).

v0.12
-----
- Added ability to specify device on command line (patch 3587144).
- Fixed segfault for -p option (patch 3587132).

v0.11
-----
- Fixed bug 3568750. Not closing devices after initial scan.

v0.10
-----
- Fixed bug 3553851. Not exiting on terminal kill. Fixed for all areas of
  program including wiping.

v0.09
-----
- Added feature #3545971. Display device name.
- Added feature #3496858. Option to not perform a final blanking pass.

v0.08
-----
- Fixed bug #3501746 whereby "wipe finished" was displayed too early

v0.07
-----
- Added threading synchronisation for logging
- Fixed bug #3486927 (incorrect Sourceforge URL)

v0.06
-----
- Added man page (thanks Michal Ambroz <rebus@seznam.cz>)
- Updated GPL licence and FSF address (thanks Michal Ambroz <rebus@seznam.cz>)

v0.05
-----
- Added sequence number to disk selection
- Added check for ncurses header files in subdir
- Fixed screen corruption bug introduced in 0.04
- Fixed occasional seg fault on start
- Introduced dynamic array allocation for devices, with no hard limit
- Minor updates to configure.ac

v0.04
-----
- Removed references to DBAN in options.c
- Added log file name option (-l|--logfile)
- If no log file specified all messages go to STDOUT
- Incorrect success message after an interruption fixed
- Improved labelling of disks with no partition table
- Added help command
- Added version command
- Added command 'b' to blank screen during wipe
- Compilation needs to include panel library

KNOWN BUG - display sometimes becomes corrupted after starting wipe

v0.03
-----
- Added quit option label (ctrl-c)
- Removed further references to DWIPE
- Added GPL V2 licence file (COPYING)

v0.02
-----
- Fixed segfault that happened during multiple disk wipes
