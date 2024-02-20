RELEASE NOTES
=============

v0.36
-----------------------
- Added the abbreviation MMC for mmcblk devices such as SD and microSD cards and some low budget laptops. #526
- Fixed some serial numbers that were displaying garbage. #527
- Fixed auto power off and nowait when the screen has been blanked by the user. #529
- Fixed nwipe not auto exiting on completion when in non gui mode. #531
- Fixed smart page titles so they have a consistent format with page 1 in the PDF report. #532
- Fixed some of the config help messages that displayed incorrect information. #533
- Inserted a space between temperature and model. #534
- Fixed incorrect footer on return to organisation/customer preview screen. #535
- Made footer completion message more informative. #538
- Fixed hidden sector detection for devices with logical/physical size of 4096/4096. #543 #546
- Fixed some strcpy compiler warnings. #548


v0.35
-----------------------
- Nwipe will now optionally create a multi-page PDF certificate that shows details of a specific discs erasure. The first page forms the certificate of erasure and subsequent pages show the drives smart data. Two related options have been added to nwipe's command line options -P, --PDFreportpath=PATH Path to write PDF reports to. Default is "." If set to "noPDF" no PDF reports are written. From the drive selection screen you can now press 'c' for config. This takes you to the configuration screen where you can select various PDF certificate related options such as enabling PDF, entering customer or company data for entry onto the certificate and enabling a preview of customer/company info prior to the drive selection screen starting.
- Nwipe now supports HPA/DCO detection, aka hidden sector detection. This is where the drive has been configured to report a smaller size to the operating system (O.S.) than it actually is. The HPA/DCO status is reported on the main drive selection screen as [HS? N/A] for drive that does not support HPA/DCO such as NvMe. [HS? YES] for a drive that is reporting a size smaller than it actually is, i.e has hidden sectors and [HS? NO] where the drive is reporting it's actual size correctly to the O.S. And finally [HS? ???] where nwipe cannot determine the HPA/DCO status as the drive is not responding to the ATA commands used to detect HPA/DCO. This might be because the drive does not support HPA/DCO or the interface adapter does not support ATA passthrough as is the case with a lot of the USB adapters on the market, but not all USB adapters. Nwipe does not currently allow removal of the HPA/DCO so you will still need to use hdparm to reset the drive so it reports its correct size before using nwipe to wipe the drive. HPA/DCO reset may be added in the next version. Thanks to @mdcato for the help testing the code and HPA/DCO results as displayed in the report.
- This bug only applies to ones wipe and one or zero's verification. A very rare occurrence of a incorrect percentage on completion. The actual wipe was completed correctly it was just that the percentage calculation was wrong. #459
- Nwipe now supports a configuration file /etc/nwipe/nwipe.conf. Currently it supports settings related to the PDF certificate but more options will be added in the future.
- If you are running nwipe within the KDE konsole terminal and you resize the window by pulling on the corners, occasionally nwipe will exit with the error message: "GUI.c,nwipe_gui_select(), loop runaway, did you close the terminal without exiting nwipe? Initiating shutdown now" The loop runaway detection has been made less sensitive, i.e 32 iterations per second of the GUI update can now be completed before a loop runaway is detected. previously it was 8. In practise when sizing the konsole window, anywhere between 1 and 17 iterations will occur.#467
- Nwipe now provides better temperature support for SAS drives. Thanks to @ggruber for all the code and testing he contributed.
- Disc sizes are now shown differently to provide more information about their size. For instance a 1.2TB drive was shown as 1TB, now it is shown as 1200GB. Thanks to @ggruber for his code contribution.
- Interface/bustype type was reported as UNK fo SAS drives, now reported correctly as SAS. Thanks to @ggruber for his code contribution.
- Interface/bustype type has been enhanced to show SAS-SSD when a SSD drive is present. Thanks to @ggruber for his code contribution.
- Nwipe's temperature retrieval code has been placed in it's own thread. This was done because it was found that any delays in obtaining the temperature resulted in a momentary freeze in the GUI wipe screen updating it's stats. This wasn't noticable if you were erasing a small number of drives but become apparent when wiping ten or twenty drives simultaneously.
v0.34
-----------------------
- Fix a compiler warning -Wformat-zero-length string 


v0.33
-----------------------
- Fixes a slight screen corruption on 80 column display. When highlighting the verify ones option the first two digits of DoD 5220.20-M disappear. This patch fixes that issue.@PartialVolume #348 
- For some controllers/drivers the readlink method of obtaining the bus type for GUI display does not work. If we haven't already resolved the bus type, we then also check smartctl for the transport protocol for SAS. @PartialVolume #350 
- Check smartctl for unresolved bus types SATA  @PartialVolume #358 
- Changed message from (No ATA pass-thru) to (S/N: unknown) as the reason the serial number is unknown is because there is no ATA pass through for the chipset being used by the USB to SATA adapter, basically we are making the message more meaningful for the end user rather than for the engineer/programmer that may understand the previous terminology used. @PartialVolume @Firminator #356 
- Add drive temperature monitoring and display temperature in degrees Celsius in the GUI. Requires the kernel drivetemp module and makes use of the hwmon sub system in the kernel to extract drive temperatures. Nwipe will automatically load the drivetemp module if it's available. @PartialVolume #360 #361 #364
- Remove /dev/ from gui for long device names. This fixes column alignment issues in the gui with nvme drives i.e. nvme0n1 etc. If the drive name including path exceeds 8 characters the /dev/ is removed and prefixed with spaces to a total max length of 8 characters. @PartialVolume #365 
- Add -q --quiet option - anonymize serial numbers and SMBIOS-DMI data. This anonymizes serial numbers and related identifiable information for drives and hardware but does not remove model information in both the GUI and the log displayed by stdout at the end of a wipe and also in the log file if enabled in options. This feature is useful for uploading logs when submitting bug reports. @PartialVolume #366 #367 #371 #379 #383 
- Fixes a intermittent FAILED message that is displayed in the summary table when the message should have been UABORTED. The incorrect FAILED message only occurred when using control-C to abort a wipe. @PartialVolume #373 
- When many verification or pass errors are detected the status line can wrap on a 80 column display. This patch makes the error message more succinct which will free up about 10 characters & prevents the line wrapping. @PartialVolume #374 
- Fixes a problem that occurs with a unresponsive drive that causes the ETA to grow to an enormous value. We now do not calculate an individual drives ETA  when the throughput of the drive is zero so avoiding the overall ETA being incorrect for drives that are working correctly when multiple drives are being simultaneously wiped. While a individual drives ETA is calculated it is not displayed but only used to determine the overall ETA when all drives have completed. @PartialVolume #375 
- Add temperature monitoring and display with NVMe drives. @PartialVolume #377 #380 #381 
- When one of the two verify only methods are selected change the drive selected text from WIPE to VRFY to indicate the drive is not being wiped, but is only being verified. @PartialVolume  #378 
- Fixes a incorrect sector, block and device sizes in 32 bit builds only as displayed in the nwipe log. This problem had no affect on the wipe as the issue was caused by a incorrect format specifier that affected the log text only. @PartialVolume #387 #388 
- Fixes a issue where temperatures may not have been available on Debian systems due to the location of modprobe. Particularly relevant to Debian which when logged in as root doesn't put /sbin in the $PATH environment setting. This issue was not necessarily relevant for Linux distros based on Debian, for instance, Ubuntu where nwipe would have found the modprobe command. @PartialVolume #390 #391 
- Improve wipe thread cancellation error checking. @PartialVolume #392 
- Improve GUI thread messaging if a pthread_join fails. @PartialVolume #393 
- Fixed a missing serial number on SAS drive.@PartialVolume  #394 
- Added ISAAC-64 for 64 bit systems. Thanks @chkboom #398 #401 
- Fixes a problem with the Gutmann wipe where the random passes at the beginning and end were being re-arranged when only the inner passes should be rearranged. Thanks @chkboom #399
- Fixes a obscure incorrect summary table status, while the log text correctly reports the failure. If the drive becomes non responsive during the wipe, the MB/s throughput will slowly drop towards 0MB/s and will display a FAILURE -1 error. The logs will correctly display errors and nwipe's return status will be non zero, however the summary table may display erased rather than FAILURE, this is because
the wipe thread exited prematurely without setting the pass error. This fixes the error by checking the context's result status, i.e non zero on failure and if pass equals zero it makes pass equal to one. This is then picked up by the summary table log code which then marks the status
correctly as FAILURE in the summary table. @PartialVolume #400 
- Fixes a spurious message on abort before wipe.This patch fixes a minor display issue that occurs when a user aborts a wipe before a wipe has started. It only occurs if the user had selected one or more drives for wipe and then aborted before starting the wipe. The spurious message only occurs in a virtual terminal, i.e. /dev/tty1, /dev/tty2, /dev/console It does not occur in terminal applications such as konsole, xterm, terminator etc. The spurious message that appears in the main window, states that "/dev/sdxyz 100% complete" along with garbage values in the statistics window. The message appears for a fraction of a second before being replaced with the textual log information that correctly states that the user aborted and no wipe was started. Basically the gui status information update function tries to update the data when the wipe hasn't even started. The fix is to only update the statistics information only if a wipe has started by checking the 'global_wipe_status' value which indicates whether any wipe started. '1' indicates that a wipe has started, else '0' if no wipe has started. @PartialVolume #406 
- Fixes temperature update in drive selection window. This fixes a problem where the drive temperature is not updated
automatically in only the drive selection window. The temperature is however updated correctly every 60 seconds during a wipe in the wipe status window. This bug would probably never be noticed by most people as usually the drive temperature changes slowly and only rises once a wipe has started. The only time I imagine it would have been noticed would have been if the drive temperature was already high and you were trying to reduce the temperature by cooling before starting a wipe. This has now been corrected so that the temperature in the drive
selection window is updated every 60 seconds. @PartialVolume #407 
- Fixes a zombie nwipe process running at 100% CPU on one core but only on a Konsole based terminal. This only occurred when the Konsole terminal is exited while nwipe is sitting at the drive selection screen but nwipe did not exit when the konsole terminal was closed. If nwipe is exited normally on completion of a wipe or aborted by using control C then this problem would not be seen. Also occurs during a wipe if the konsole terminal is closed without exiting nwipe first, again only on Konsole based terminals. @PartialVolume #408 #409 
- Fixes a obscure segfault when --logfile option used with a non writable directory. @PartialVolume #410 


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
