#!/bin/sh
# Build a tarball from the latest upstream version, with a nice
# version number.
#
# Requires git 1.6.6 or later, GNU date, and gzip.

set -e

trap 'rm -fr debian-orig-source || exit 1' EXIT INT TERM

svn export -r${REV} http://svn.openmoko.org/trunk/eda/fped debian-orig-source

# Determine version number.
release=0.0
upstream_version="${release}+r${REV}"

# Generate tarball.
echo "packaging ..."
tar -czf fped_$upstream_version.orig.tar.gz debian-orig-source
