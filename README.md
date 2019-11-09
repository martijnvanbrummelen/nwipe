# nwipe

nwipe is a command that will securely erase disks using a variety of
recognised methods.  It is a fork of the dwipe command used by
Darik's Boot and Nuke (dban).  nwipe is included with partedmagic if you
want a quick and easy bootable CD version.  nwipe was created out of
a need to run the DBAN dwipe command outside of DBAN, in order to
allow its use with any host distribution, thus giving better hardware
support.

## Compiling & Installing

`nwipe` requires the following libraries to be installed:

* ncurses
* pthreads
* parted

### Debian & Ubuntu prerequisites

If you are compiling `nwipe` from source, the following libraries will need to be installed first:

```bash
sudo apt install \
  build-essential \
  pkg-config \
  automake \
  libncurses5-dev \
  autotools-dev \
  libparted-dev
```

### Fedora prerequisites

```bash
sudo bash
dnf update
dnf groupinstall "Development Tools"
dnf groupinstall "C Development Tools and Libraries"
yum install ncurses-devel
yum install parted-devel
```

### Compilation

For a development setup, see [the hacking section below](#Hacking).

First create all the autoconf files:
```
./init.sh
```

Then compile & install using the following standard commands:
```
./configure
make
make install
```

### Hacking

If you wish to submit pull requests to this code we would prefer you enable all warnings when compiling.
This can be done using the following compile commands:

```
./configure --prefix=/usr CFLAGS='-O0 -g -Wall -Wextra'
make
make install
```

The `-O0 -g` flags disable optimisations. This is required if you're debugging with
`gdb` in an IDE such as Kdevelop. With these optimisations enabled you won't be
able to see the values of many variables in nwipe, not to mention the IDE won't step
through the code properly.

The `-Wall` and `-Wextra` flags enable all compiler warnings. Please submit code with zero warnings.

Once done with your coding then the released/patch/fixed code can be compiled,
with all the normal optimisations, using:
```
./configure --prefix=/usr && make && make install
```

## Bugs

Bugs can be reported on GitHub:
https://github.com/martijnvanbrummelen/nwipe

## License

GNU General Public License v2.0
