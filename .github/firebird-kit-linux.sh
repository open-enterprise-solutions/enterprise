#!/usr/bin/env bash
# The Firebird 5 kit for Linux x64 — the official build, PINNED, laid out as Firebird's own tree (lib/,
# plugins/, intl/, tzdata/, firebird.msg, …) in ./fbkit. A program finds it as `_fb/` beside itself and loads
# `_fb/lib/libfbclient.so.2` by path (firebirdInterface.cpp); the kit's libraries find each other through
# `$ORIGIN/../lib`.
#
# ONE KIT FOR TWO USERS: the nightly package ships it (nightly.yml), and the CI job that runs the tests
# against a live Firebird lays it out beside the test binary (ci.yml, tests-firebird) — so what the tests
# ran on is what the package carries.
#
# The one library the kit does not carry is libtommath, which Ubuntu does not install by default; it is
# taken from the runner's own package and put beside the kit's libraries. The plugins the Windows kit leaves
# out (profiler, trace, UDR) are left out here too, and firebird.conf is the Windows kit's own —
# SuperClassic, so two programs can open one base.
#
# Run from the repository root. Leaves ./fbkit (the kit) and ./fb5root (the unpacked build —
# package-linux.sh takes isql from it for its smoke test).
set -euo pipefail

curl -fsSL -o fb5.tar.gz "https://github.com/FirebirdSQL/firebird/releases/download/v5.0.4/Firebird-5.0.4.1812-0-linux-x64.tar.gz"
tar xzf fb5.tar.gz
mkdir -p fb5root fbkit
tar xzf Firebird-5.0.4.1812-0-linux-x64/buildroot.tar.gz -C fb5root
fb=fb5root/opt/firebird
cp -a "$fb/lib" "$fb/plugins" "$fb/intl" "$fb/tzdata" "$fb/firebird.msg" "$fb/security5.fdb" "$fb/plugins.conf" fbkit/
rm -rf fbkit/plugins/udr fbkit/plugins/libDefault_Profiler.so fbkit/plugins/libfbtrace.so \
       fbkit/plugins/libudr_engine.so fbkit/plugins/udr_engine.conf
cp src/engine/backend/databaseLayer/firebird/engine/dll/firebird.conf fbkit/
apt-get download libtommath1
dpkg-deb -x libtommath1_*.deb tommath
cp -a tommath/usr/lib/x86_64-linux-gnu/libtommath.so.1* fbkit/lib/
ls -la fbkit fbkit/lib fbkit/plugins
