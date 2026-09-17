#!/usr/bin/env bash
# Pack a CMake Release tree for Linux x64 into the nightly tarball.
#
# Usage: package-linux.sh <build>/bin <out.tar.gz>
#
# What goes in: everything the build put beside the executables (programs, libbackend / libfrontend, the
# wxWidgets libraries with their version symlinks, help/, plugins/), plus the translations (lang/),
# backend.conf, and the Firebird 5 kit the workflow laid out in fbkit/ — as _fb/, where the engine loads it.
#
# ⚠ THE LIBRARIES ARE FOUND BESIDE THE PROGRAM, NOT WHERE THEY WERE BUILT. A build-tree binary carries the
# ABSOLUTE build directory as its RUNPATH, so unpacked anywhere else it finds neither libbackend nor wx.
# Every ELF file gets `$ORIGIN` (`$ORIGIN/..` under plugins/). RUNPATH is not inherited, so the libraries
# get it too — libwx_gtk3u_core finds libwx_baseu through its own entry, not the executable's.
#
# It REFUSES rather than packs a tree that would not start: after the rewrite, every library our own files
# ask for has to resolve inside the package or on the system (ldd), or the job fails.
set -euo pipefail

bin="$1"
out="$2"
release="$bin/Release"
stage="$PWD/oes"

rm -rf "$stage"
mkdir -p "$stage"
cp -a "$release/." "$stage/"
cp -a lang "$stage/lang"
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

required=(enterprise designer launcher codeRunner daemon libbackend.so libfrontend.so backend.conf help/en.hlk lang/ru/open_es.mo
          _fb/lib/libfbclient.so.2 _fb/lib/libtommath.so.1 _fb/plugins/libEngine13.so _fb/intl/fbintl _fb/firebird.msg _fb/firebird.conf)
for r in "${required[@]}"; do
  [ -e "$stage/$r" ] || { echo "::error::The package would not run - missing: $r"; exit 1; }
done

# Resolve, from a clean environment, what our own files need.
unresolved=0
for f in "$stage"/enterprise "$stage"/designer "$stage"/launcher "$stage"/codeRunner "$stage"/daemon "$stage"/libbackend.so "$stage"/libfrontend.so \
         "$stage"/_fb/lib/libfbclient.so.2 "$stage"/_fb/plugins/libEngine13.so "$stage"/_fb/intl/fbintl; do
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

# What the user's system has to provide — everything the programs load from outside the package.
{
  echo '### Linux: libraries the package takes from the system'
  echo '```'
  for f in "$stage"/designer "$stage"/enterprise "$stage"/libbackend.so "$stage"/libfrontend.so; do
    ldd "$f"
  done | awk '$3 ~ /^\// && $3 !~ /\/oes\// {print $1}' | sort -u
  echo '```'
} >> "${GITHUB_STEP_SUMMARY:-/dev/stdout}"

tar -czf "$out" -C "$PWD" oes
ls -la "$out"
