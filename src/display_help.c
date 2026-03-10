#include "stdio.h"
#include "ANSI-color-codes.h"

void display_help()
{
    /************************************************
     * displays the help section to STDOUT and exits.
     */

    /* Limit the line length to a maximum of 80 printable characters so it looks good in 80x25 terminals
     * on a 4:3 ratio monitor. Note some lines include escape seqences for color coding of characters so
     * they may appear below longer than they actually are when printed. Case in point, line 164 --nousb..
     * that contains esc sequences to color code --nogui.
     * "        Do NOT show or wipe any USB devices whether in GUI mode, " BHCYN "--nogui" reset " or\n" \
     */

    /*        10        20        30        40        50        60        70        80
     *2345678901234567890123456789012345678901234567890123456789012345678901234567890< Do not exceed
     */

    printf( "Usage: nwipe [options] [device1] [device2] ... \n" \
    BHCYN \
    "Options:\n" reset \
    BHCYN \
    "  -V, --version\n" reset \
    "        Prints the version number\n\n" \
    BHCYN \
    "  -v, --verbose\n" reset \
    "        Prints more messages to the log\n\n" \
    BHCYN \
    "  -h, --help\n" reset \
    "        Prints this help\n\n" \
    BHCYN \
    "      --force\n" reset \
    "        Also allow wiping of devices that are considered in use (mounted).\n" \
    "        Beware this option is considered dangerous and is disabled by default.\n" \
    "        This means that by default even --autonuke will exclude any such devices.\n\n" \
    BHCYN \
    "      --autonuke\n" reset \
    "        If no devices have been specified on the command line, starts wiping\n" \
    "        all devices immediately. If devices have been specified, starts wiping\n" \
    "        only those specified devices immediately.\n\n" \
    BHCYN \
    "      --autopoweroff\n" reset \
    "        Power off system on completion of wipe delayed for one minute. During\n" \
    "        this one minute delay you can abort the shutdown by typing\n" \
    "        sudo shutdown -c\n\n" \
    BHCYN \
    "      --sync=NUM\n" reset \
    "        Will perform a sync after NUM writes (default: 100000)\n" \
    BHCYN \
    "        0" reset " - fdatasync after the disk is completely written. fdatasync\n" \
    "        errors are not detected until completion of the wipe. 0 is not\n" \
    "        recommended as disk errors may cause nwipe to appear to hang\n" \
    BHCYN \
    "        1" reset " - fdatasync after every write\n" \
    BRED \
    "        Warning: " reset "Lower values will reduce wipe speeds.\n" \
    "        1000 - fdatasync after 1000 writes etc.\n" \
    "        Note: Sync is not used when nwipe is operating in direct I/O mode.\n\n" \
    BHCYN \
    "      --verify=TYPE\n" reset \
    "        Whether to perform verification of erasure (default: last)\n" \
    BHCYN \
    "        off" reset "  - Do not verify\n" \
    BHCYN \
    "        last" reset " - Verify after the last pass\n" \
    BHCYN \
    "        all" reset "  - Verify every pass\n" \
    "        Be aware that HMG IS5 enhanced always verifies the last (PRNG) pass\n" \
    "        regardless of this option.\n\n" \
    BHCYN \
    "      --directio\n" reset \
    "        Force direct I/O (O_DIRECT); fail if not supported\n" \
    "        Note: nwipe's default I/O method is to use direct I/O\n\n" \
    BHCYN \
    "      --cachedio\n" reset \
    "        Force kernel cached I/O; never attempt O_DIRECT\n\n" \
    BHCYN \
    "      --io-mode=MODE\n" reset \
    "        I/O mode: auto (default), direct, cached\n\n" \
    BHCYN \
    "  -m, --method=METHOD\n" reset \
    "        The wiping method. See man page for more details. (default: PRNG)\n" \
    BHCYN \
    "        dod522022m | dod" reset "       - 7 pass DOD 5220.22-M method\n" \
    BHCYN \
    "        dodshort | dod3pass" reset "    - 3 pass DOD method\n" \
    BHCYN \
    "        gutmann" reset "                - Peter Gutmann's Algorithm\n" \
    BHCYN \
    "        ops2" reset "                   - RCMP TSSIT OPS-II\n" \
    BHCYN \
    "        random | prng | stream" reset " - PRNG Stream\n" \
    BHCYN \
    "        zero | quick" reset "           - Overwrite with zeros\n" \
    BHCYN \
    "        one" reset "                    - Overwrite with ones (0xFF)\n" \
    BHCYN \
    "        verify_zero" reset "            - Verifies disk is zero filled\n" \
    BHCYN \
    "        verify_one" reset "             - Verifies disk is 0xFF filled\n" \
    BHCYN \
    "        is5enh" reset "                 -  HMG IS5 enhanced\n" \
    BHCYN \
    "        bruce7" reset "                 -  Schneier Bruce 7-pass mixed pattern\n" \
    BHCYN \
    "        bmb" reset "                    -  BMB21-2019 mixed pattern\n\n" \
    BHCYN \
    "  -l, --logfile=FILE\n" reset \
    "        Filename to log to. Default is STDOUT\n\n" \
    BHCYN \
    "  -P, --PDFreportpath=PATH\n" reset \
    "        Path to write PDF reports to. Default is '.'\n" \
    "        If set to" BHCYN " noPDF " reset "no PDF reports are written.\n\n" \
    BHCYN \
    "  -p, --prng=TYPE\n" reset \
    "        PRNG option:\n" \
    BHCYN \
    "        mersenne | twister\n" \
    "        isaac\n" \
    "        isaac64\n" \
    "        add_lagg_fibonacci_prng\n" \
    "        xoroshiro256_prng\n" \
    "        splitmix64\n" \
    "        aes_ctr_prng\n" \
    "        chacha20\n\n" \
    BHCYN \
    "      --prng=auto|default\n" \
    "        auto" reset " - Automatically benchmark all available PRNGs at startup\n" \
    "        and select the fastest one for the current hardware. (default=auto)\n"  \
    BHCYN \
    "        default|manual" reset " - Disable auto-selection and use the built-in\n" \
    "        default PRNG choice (CPU-based heuristic; no benchmarking).\n\n" \
    BHCYN \
    "      --prng-benchmark\n" reset \
    "        Run a RAM-only PRNG throughput benchmark and exit.\n" \
    "        Prints a sorted leaderboard (MB/s). No wipe is performed.\n\n" \
    BHCYN \
    "      --prng-bench-seconds=N\n" reset \
    "        Seconds per PRNG during benchmarking (default: 1.0).\n" \
    "        For --prng=auto this is automatically reduced unless set.\n\n" \
    BHCYN \
    "  -q, --quiet\n" reset \
    "        Anonymize logs and the GUI by removing unique data, i.e. serial\n" \
    "        numbers, LU WWN Device ID, and SMBIOS/DMI data. XXXXXX = S/N exists,\n" \
    "        ????? = S/N not obtainable.\n\n" \
    BHCYN \
    "  -r, --rounds=NUM\n" reset \
    "        Number of times to wipe the device using the selected method.\n" \
    "        (default: 1)\n\n" \
    BHCYN \
    "      --noblank\n" reset \
    "        Do NOT blank disk after wipe.\n" \
    "        (default is to complete a final blank pass)\n\n" \
    BHCYN \
    "      --nowait\n" reset \
    "        Do NOT wait for a key before exiting.\n" \
    "        (default is to wait)\n\n" \
    BHCYN \
    "      --nosignals\n" reset \
    "        Do NOT allow signals to interrupt a wipe.\n" \
    "        (default is to allow)\n\n" \
    BHCYN \
    "      --nogui\n" reset \
    "        Do NOT show the GUI interface. Automatically invokes the nowait\n" \
    "        option. Must be used with the" BHCYN " --autonuke " reset "option.\n" \
    "        Send a `kill -signal SIGUSR1 pid` to display current stats in the\n" \
    "        terminal.\n\n" \
    BHCYN \
    "      --nousb\n" reset \
    "        Do NOT show or wipe any USB devices whether in GUI mode, " BHCYN "--nogui" reset " or\n" \
    "        " BHCYN "--autonuke modes.\n\n" reset \
    BHCYN \
    "      --reverse\n" reset \
    "        Reverse the I/O direction (from the end of the device towards the start)\n" \
    "        Helpful when bad blocks otherwise prevent wiping major areas of the device\n" \
    "        Beware throughput may be degraded as it writes against the spin direction\n\n" \
    BHCYN \
    "      --no-retry-on-io-errors\n" reset \
    "        Do NOT retry single failed read/write operations; immediately error\n" \
    "        or skip the failing block. (default is retry 3 times with 5s sleep\n" \
    "        in-between)\n\n" \
    BHCYN \
    "      --no-abort-on-block-errors\n" reset \
    "        Do NOT abort passes on block write errors; skip the failing block and\n" \
    "        continue. (default is to abort the wipe for that device)\n" \
    BRED \
    "        Beware:" reset " Faulty devices can take a very long time to wipe\n" \
    "        without the --no-retry-on-io-errors option, due to the many retries.\n\n" \
    BHCYN \
    "      --pdftag\n" reset \
    "        Enables a field on the PDF that holds a tag that identifies the host\n"
    "        computer.\n\n" \
    BHCYN \
    "  -e, --exclude=DEVICES\n" reset \
    "        Up to ten comma separated devices to be excluded.\n" \
    "        Examples:\n" \
    "        --exclude=/dev/sdc\n" \
    "        --exclude=/dev/sdc,/dev/sdd\n" \
    "        --exclude=/dev/sdc,/dev/sdd,/dev/mapper/cryptswap1\n\n" \
    "        --exclude=/dev/disk/by-id/ata-XXXXXXXX\n" \
    "        --exclude=/dev/disk/by-path/pci-0000:00:17.0-ata-1\n\n");
}
