#!/bin/sh
# Regenerate the build system (configure, Makefile.in, ...).
# Requires autoconf >= 2.69 and automake >= 1.16. After this, run:
#     ./configure && make
set -e
cd "$(dirname "$0")"
autoreconf --install --force
echo "autogen done — now run: ./configure && make"
