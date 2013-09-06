#!/bin/bash

# Script to create all the required autoconf files

aclocal
autoheader
automake --missing
autoconf
