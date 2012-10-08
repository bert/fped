#!/bin/sh
# Build a tarball from the latest upstream version, with a nice
# version number.
#
# Requires git 1.6.6 or later, GNU date, and gzip.

set -e

: ${REPO=git://projects.qi-hardware.com/fped.git}
: ${BRANCH=remotes/origin/master}

mkdir debian-orig-source
trap 'rm -fr debian-orig-source || exit 1' EXIT

git init -q debian-orig-source
GIT_DIR=$(pwd)/debian-orig-source/.git
export GIT_DIR

# Fetch latest upstream version.
git fetch -q "$REPO" "$BRANCH"

# Determine version number.
release=0.1
date=$(date --utc --date="$(git log -1 --pretty=format:%cD FETCH_HEAD)" "+%Y%m")
upstream_version="${release}+${date}"

# Generate tarball.
echo "packaging $(git rev-parse --short FETCH_HEAD)"
git archive --format=tar --prefix="fped_${date}/" FETCH_HEAD |
	gzip -n -9 >"fped_$upstream_version.orig.tar.gz"
