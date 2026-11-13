#!/usr/bin/env bash
# The Firebird 5 kit for macOS arm64 — the official build, PINNED, laid out as Firebird's own tree (lib/ with
# ICU and tommath, plugins/, intl/, tzdata/, firebird.msg, …) in ./fbkit. It ships as an installer package;
# `pkgutil --expand-full` unpacks it without installing anything, and the framework's Resources are the
# whole tree. A program finds it as `_fb/` beside itself and loads `_fb/lib/libfbclient.dylib` by path
# (firebirdInterface.cpp); the kit's libraries name each other `@rpath/lib/…` and carry no rpath of their
# own, so the images that load them answer: libcore's `@loader_path/_fb` for fbclient, the program's own
# rpath for the engine plugin fbclient loads - in the build tree both from CMakeLists.txt, in the package
# from package-macos.sh.
#
# ONE KIT FOR TWO USERS, as on Linux (firebird-kit-linux.sh): the nightly package ships it (nightly.yml),
# and the CI job that shoots the desktop lays it out beside the programs (ci.yml, tests-macos-arm), so the
# designer and the application there open a file base on what the package carries.
#
# The plugins the Windows kit leaves out (profiler, trace, UDR, the samples) are left out here too, and
# firebird.conf is the Windows kit's own — SuperClassic, so two programs can open one base.
#
# Run from the repository root. Leaves ./fbkit (the kit) and ./fb5pkg (the unpacked package —
# package-macos.sh takes isql from it for its smoke test).
set -euo pipefail

curl -fsSL -o fb5.pkg "https://github.com/FirebirdSQL/firebird/releases/download/v5.0.4/Firebird-5.0.4.1812-0-macos-arm64.pkg"
pkgutil --expand-full fb5.pkg fb5pkg
res=fb5pkg/Firebird.pkg/Payload/Versions/A/Resources
mkdir -p fbkit
for item in lib plugins intl tzdata firebird.msg security5.fdb plugins.conf; do
  if [ -e "$res/$item" ]; then cp -a "$res/$item" fbkit/; fi
done
rm -rf fbkit/plugins/udr fbkit/plugins/libDefault_Profiler.dylib fbkit/plugins/libfbtrace.dylib \
       fbkit/plugins/libudr_engine.dylib fbkit/plugins/udr_engine.conf \
       fbkit/plugins/libfbSample* fbkit/plugins/fbSample*
cp src/engine/backend/databaseLayer/firebird/engine/dll/firebird.conf fbkit/
ls -la fbkit fbkit/lib fbkit/plugins
