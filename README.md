nwipe is a command that will securely erase disks using a variety of
recognised methods.  It is a fork of the dwipe command used by
Darik's Boot and Nuke (dban).  nwipe is included with partedmagic if you
want a quick and easy bootable CD version.  nwipe was created out of
a need to run the DBAN dwipe command outside of DBAN, in order to
allow its use with any host distribution, thus giving better hardware
support.

To use from the git repository, first create all the autoconf files with
./init.sh

Then do the standard ./configure --prefix=/usr && make && make install

For developer & release notes please see the [README file](README)

