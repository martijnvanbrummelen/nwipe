nwipe is a command that will securely erase disks using a variety of
recognised methods.  It is a fork of the dwipe command used by
Darik's Boot and Nuke (dban).  nwipe is included with partedmagic if you
want a quick and easy bootable CD version.  nwipe was created out of
a need to run the DBAN dwipe command outside of DBAN, in order to
allow its use with any host distribution, thus giving better hardware
support.

To use from the git repository, first install all the required packages
```
sudo apt install build-essential
sudo apt install automake
sudo apt install pkg-config
sudo apt install ncurses-dev
sudo apt install libparted-dev
```
Then as normal user create all the autoconf files with
```
./init.sh
```
Then do the standard ..
```
./configure --prefix=/usr
make
sudo make install
```
However, if you're a developer and want to work on this code, you may prefer to use
```
./configure --prefix=/usr CFLAGS='-O0 -g -Wall -Wextra'
make
sudo make install
```
The '-O0 -g' flags disable optimisations, this is required if your debugging with
gdb in an IDE such as Kdevelop. Without these optimisations disabled you won't be
able to see the values of many variables in nwipe, not to mention the IDE won't step
through the code properly.

The -Wall & -Wextra flags enable all compiler warnings. Unfortunately as of 25/Sep/2018
there are quite a few warnings still present. We're hoping to clear these warnings
(the proper way with a code review), but it would be nice to not have any more code
added that generates warnings. Most of these warnings are benign, however some do
highlight bugs, such as the "variable used before being initialised".

Once done with your coding then the released/patch/fixed code can be compiled with
```
./configure --prefix=/usr
make
sudo make install
```
complete with all it's optimisations.

For release notes please see the [README file](README)

