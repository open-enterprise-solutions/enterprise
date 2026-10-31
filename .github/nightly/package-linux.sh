#!/usr/bin/env bash
# Pack a CMake Release tree for Linux (x64 or arm64, whichever it was built on) into the nightly tarball.
#
# Usage: package-linux.sh <build>/bin <out.tar.gz>
#
# What goes in: everything the build put beside the executables (programs, libbackend / libfrmclient, the
# wxWidgets libraries with their version symlinks, help/, plugins/), plus the translations (lang/),
# backend.conf, and the Firebird 5 kit the workflow laid out in fbkit/ — as _fb/, where the engine loads it.
#
# ⚠ THE LIBRARIES ARE FOUND BESIDE THE PROGRAM, NOT WHERE THEY WERE BUILT. A build-tree binary carries the
# ABSOLUTE build directory as its RUNPATH, so unpacked anywhere else it finds neither libbackend nor wx.
# Every ELF file gets `$ORIGIN` (`$ORIGIN/..` under plugins/). RUNPATH is not inherited, so the libraries
# get it too — libwx_gtk3u_core finds libwx_baseu through its own entry, not the executable's.
#
# It REFUSES rather than packs a tree that would not start: after the rewrite, every library our own files
# ask for has to resolve inside the package or on the system (ldd) - on this one, and on a clean Debian 12
# (docker) - or the job fails.
set -euo pipefail

bin="$1"
out="$2"
release="$bin/Release"
stage="$PWD/oes"

rm -rf "$stage"
mkdir -p "$stage"
cp -a "$release/." "$stage/"
cp -a lang "$stage/lang"
find "$stage/lang" \( -name '*.po' -o -name '*.pot' \) -delete   # the programs read the .mo only
cp src/engine/backend/backend.conf "$stage/backend.conf"
# The Firebird 5 kit the workflow laid out (fbkit/), as Firebird's own tree — see nightly.yml.
cp -a fbkit "$stage/_fb"

is_elf() { [ -f "$1" ] && [ ! -L "$1" ] && [ "$(head -c 4 "$1" | od -An -tx1 | tr -d ' ')" = "7f454c46" ]; }

# Our files only: the kit already says `$ORIGIN/../lib` for itself, and that is what makes it movable.
while IFS= read -r -d '' f; do
  if is_elf "$f"; then
    case "$f" in
      "$stage"/_fb/*)     ;;
      "$stage"/plugins/*) patchelf --set-rpath '$ORIGIN/..' "$f" ;;
      *)                  patchelf --set-rpath '$ORIGIN' "$f" ;;
    esac
  fi
done < <(find "$stage" -type f -print0)

required=(enterprise-thin designer-thin launcher codeRunner appserver
          appcluster appcluster-tray webserver web/www.wpk
          libcore.so libbackend.so libprotocol.so libfrmclient.so libfrmserver.so libfileserver.so
          backend.conf help/en.hlk lang/ru/open_es.mo
          _fb/lib/libfbclient.so.2 _fb/lib/libtommath.so.1 _fb/plugins/libEngine13.so _fb/intl/fbintl _fb/firebird.msg _fb/firebird.conf)
for r in "${required[@]}"; do
  [ -e "$stage/$r" ] || { echo "::error::The package would not run - missing: $r"; exit 1; }
done

# Resolve, from a clean environment, what our own files need.
unresolved=0
for f in "$stage"/enterprise-thin "$stage"/designer-thin "$stage"/launcher "$stage"/codeRunner "$stage"/appserver \
         "$stage"/appcluster "$stage"/appcluster-tray "$stage"/webserver \
         "$stage"/libcore.so "$stage"/libbackend.so "$stage"/libprotocol.so "$stage"/libfrmclient.so \
         "$stage"/libfrmserver.so "$stage"/libfileserver.so "$stage"/_fb/lib/libfbclient.so.2 "$stage"/_fb/plugins/libEngine13.so "$stage"/_fb/intl/fbintl; do
  if env -u LD_LIBRARY_PATH ldd "$f" | grep -q 'not found'; then
    echo "::error::$(basename "$f") has unresolved libraries:"
    env -u LD_LIBRARY_PATH ldd "$f" | grep 'not found'
    unresolved=1
  fi
done
[ "$unresolved" -eq 0 ] || exit 1

# ⭐ THE KIT OPENS A BASE WHERE IT STANDS. Resolved libraries are not a working engine: the engine plugin,
# the character sets and ICU are loaded at run time, through FIREBIRD. So Firebird's own isql, from the same
# release, is put into the kit for a minute and creates a UTF8 base embedded — a case-insensitive Unicode
# collation, so ICU has to answer — writes a row and reads it back. Then it is taken out again.
smoke_dir="$(mktemp -d)"
mkdir -p "$stage/_fb/bin"
cp -a fb5root/opt/firebird/bin/isql "$stage/_fb/bin/isql"
cat > "$smoke_dir/smoke.sql" <<SQL
CREATE DATABASE '$smoke_dir/smoke.fdb' USER 'SYSDBA' DEFAULT CHARACTER SET UTF8;
CREATE TABLE T (S VARCHAR(20) CHARACTER SET UTF8 COLLATE UNICODE_CI);
COMMIT;
INSERT INTO T VALUES ('ABC');
COMMIT;
SELECT COUNT(*) AS FOUND FROM T WHERE S = 'abc';
SQL
if ! FIREBIRD="$stage/_fb" "$stage/_fb/bin/isql" -q -i "$smoke_dir/smoke.sql" > "$smoke_dir/out.txt" 2>&1 \
   || ! grep -Eq '^[[:space:]]*1[[:space:]]*$' "$smoke_dir/out.txt"; then
  echo "::error::The Firebird kit in the package does not open a base:"
  cat "$smoke_dir/out.txt"
  exit 1
fi
echo "Firebird kit: an embedded UTF8 base created, written and read in place."
rm -rf "$stage/_fb/bin" "$smoke_dir"

# ⭐ AND THEY RESOLVE ON A CLEAN SYSTEM — the ldd above cannot say so. It runs on the build machine, whose
# development packages answer for what a user's system lacks: libpcre2-32 resolved here, and the demo's web server
# did not start on Debian 12 (2026-10-29). So the package is resolved again in a bare Debian 12, in two steps, as
# the release note promises it: the server's programs - and libfileserver, which the web server loads when it
# opens a file base - with GLib alone (wx's base library asks for it); then everything, with GTK 3 installed and
# the two X libraries wx links beside it (session management, XTest), which every desktop has.
clean_check='
apt-get update -qq > /dev/null || { echo "apt: the package lists cannot be read"; exit 0; }
apt-get install -y -qq --no-install-recommends libglib2.0-0 > /dev/null || { echo "apt: GLib cannot be installed"; exit 0; }
for f in /oes/webserver /oes/appserver /oes/appcluster /oes/libfileserver.so; do ldd "$f"; done 2>&1 \
  | grep "not found" | sed "s/^/server: /" | sort -u
apt-get install -y -qq --no-install-recommends libgtk-3-0 libsm6 libxtst6 > /dev/null || { echo "apt: GTK 3 cannot be installed"; exit 0; }
find /oes -path /oes/_fb -prune -o -type f -print | while read -r f; do
  if [ "$(head -c 4 "$f" | od -An -tx1 | tr -d " ")" = "7f454c46" ]; then ldd "$f" 2>&1; fi
done | grep "not found" | sed "s/^/desktop: /" | sort -u
'
missing="$(docker run --rm -v "$stage:/oes:ro" debian:12-slim sh -c "$clean_check")"
if [ -n "$missing" ]; then
  echo "::error::The package does not start on a clean Debian 12 - missing:"
  echo "$missing"
  exit 1
fi
echo "Clean Debian 12: the server's programs resolve with GLib alone, the rest with GTK 3."

# ⭐ AND NONE OF OUR LIBRARIES BUT CORE ASKS FOR STATIC TLS — which ldd does not see either. A library a program
# LOADS (libfileserver and the backend with it, when a file base is opened; a plugin) gets some 512 bytes of the
# static TLS block, and one initial-exec variable makes its whole TLS static: "libbackend.so: cannot allocate
# memory in static TLS block" was the demo server's first night (2026-10-30). Core is linked by every program,
# so its fixed offset (memoryPool.cpp) is reserved at start.
static_tls=""
for f in "$stage"/lib*.so "$stage"/plugins/*.so; do
  [ -e "$f" ] && [ "$(basename "$f")" != libcore.so ] || continue
  # grep reads to the end: with -q it would stop early, and under pipefail readelf's SIGPIPE would hide the find
  readelf -d "$f" | grep 'STATIC_TLS' > /dev/null && static_tls="$static_tls $(basename "$f")"
done
if [ -n "$static_tls" ]; then
  echo "::error::Static TLS (an initial-exec thread_local) in a library a program may dlopen:$static_tls"
  exit 1
fi

# What the user's system has to provide — everything the programs load from outside the package.
{
  echo '### Linux: libraries the package takes from the system'
  echo '```'
  for f in "$stage"/designer-thin "$stage"/enterprise-thin "$stage"/libbackend.so "$stage"/libfrmclient.so; do
    ldd "$f"
  done | awk '$3 ~ /^\// && $3 !~ /\/oes\// {print $1}' | sort -u
  echo '```'
} >> "${GITHUB_STEP_SUMMARY:-/dev/stdout}"

tar -czf "$out" -C "$PWD" oes
ls -la "$out"
