#!/bin/sh
# Build a tarball from the latest upstream version, with a nice
# version number.

set -e

# Determine version number.
release=0.0
upstream_version="${release}+r${REV}"

TOPFOLDER=fped-$upstream_version.orig

trap 'rm -fr ${TOPFOLDER} || exit 1' EXIT INT TERM

svn export -r${REV} http://svn.openmoko.org/trunk/eda/fped ${TOPFOLDER}

# Generate tarball.
echo "packaging ..."
tar -czf fped_$upstream_version.orig.tar.gz ${TOPFOLDER}
