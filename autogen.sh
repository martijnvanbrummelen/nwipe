#!/bin/bash

# Script to create all the required autoconf files

aclocal
autoheader
automake --add-missing
autoconf
