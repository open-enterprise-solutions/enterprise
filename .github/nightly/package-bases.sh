#!/usr/bin/env bash
# Pack the example bases into the nightly's bases tarball.
#
# Usage: package-bases.sh <examples> <out.tar.gz>
#
# What goes in: every folder of <examples> that holds an infobase.conf - that file is what makes a folder a base -
# as bases/<folder>/, whole. The demo server (tools/demo-server) publishes each one under its folder's name.
#
# ⭐ THE BASES TRAVEL WITH THE PROGRAMS OF THE SAME COMMIT. A base that needs tonight's engine lands in the commit
# that brings the engine, and the nightly packs both from it - so a server that takes them from one release never
# opens a base with a build that does not know it.
#
# It REFUSES rather than packs nothing: a release with no bases would leave the demo with nothing to show.
set -euo pipefail

examples="$1"
out="$2"
stage="$PWD/bases"

rm -rf "$stage"
mkdir -p "$stage"

count=0
for conf in "$examples"/*/infobase.conf; do
  [ -e "$conf" ] || continue
  base="$(dirname "$conf")"
  cp -a "$base" "$stage/$(basename "$base")"
  echo "base: $(basename "$base")"
  count=$((count + 1))
done
[ "$count" -gt 0 ] || { echo "::error::No base in $examples - no folder holds an infobase.conf"; exit 1; }

tar -czf "$out" -C "$PWD" bases
ls -la "$out"
