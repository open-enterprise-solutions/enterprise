#!/usr/bin/env bash
# Pack a CMake Release tree for macOS arm64 into the nightly disk image.
#
# Usage: package-macos.sh <build>/bin <out.dmg>
#
# The image holds ONE folder, `OES`, and a link to /Applications: the folder is dragged as a whole. The
# applications do not carry their libraries inside the bundles — enterprise.app, designer.app and
# launcher.app stand beside libbackend / libfrontend / the wxWidgets dylibs, codeRunner and daemon, and the
# launcher finds the others as siblings — so the folder is what installs, not an .app alone.
#
# ⚠ THE LIBRARIES ARE FOUND BESIDE THE PROGRAM, NOT WHERE THEY WERE BUILT. Every dylib is named
# `@rpath/…`, and a build-tree binary's rpath is the ABSOLUTE build directory. Rewritten here:
#   a bundle's executable   X.app/Contents/MacOS/X   @executable_path/../../..
#   a flat executable                                  @executable_path
#   a dylib (wx libraries load each other through it) @loader_path
#   a plugin under plugins/                            @loader_path/..
# The absolute build paths are removed; the Firebird framework paths the build adds stay, since that is
# where an installed Firebird 5 lives.
#
# Editing a Mach-O breaks the linker's ad-hoc signature, and arm64 refuses to run an unsigned binary, so
# every file is signed again (ad hoc — not a Developer ID; see the release note on opening it).
#
# It REFUSES rather than packs a tree that would not start: every `@rpath/` dependency of every file has
# to resolve inside the package, or the job fails.
set -euo pipefail

bin="$1"
out="$2"
release="$bin/Release"
root="$PWD/dmgroot"
stage="$root/OES"
build_dir="$(cd "$bin/.." && pwd)"

rm -rf "$root"
mkdir -p "$stage"
cp -a "$release/." "$stage/"
cp -a lang "$stage/lang"
cp src/engine/backend/backend.conf "$stage/backend.conf"
# The Firebird 5 kit the workflow laid out (fbkit/): the framework's Resources, as Firebird's own tree.
cp -a fbkit "$stage/_fb"

is_macho() {
  [ -f "$1" ] && [ ! -L "$1" ] || return 1
  case "$(head -c 4 "$1" | od -An -tx1 | tr -d ' ')" in
    cffaedfe|cefaedfe|cafebabe) return 0 ;;
    *) return 1 ;;
  esac
}

rpaths_of() { otool -l "$1" | awk '/cmd LC_RPATH/ {getline; getline; print $2}'; }

# fix_rpath <file> <rpath>... — drop the build directory's absolute rpaths and the installed Firebird
# framework's (the kit beside the program is the one to find, not whatever else is on the machine), then
# add the wanted ones in the order given.
fix_rpath() {
  local f="$1" r want; shift
  while IFS= read -r r; do
    case "$r" in
      "$build_dir"*|"$PWD"*|/Library/Frameworks/Firebird.framework*) install_name_tool -delete_rpath "$r" "$f" ;;
    esac
  done < <(rpaths_of "$f")
  for want in "$@"; do
    if ! rpaths_of "$f" | grep -qxF "$want"; then
      install_name_tool -add_rpath "$want" "$f"
    fi
  done
}

# Our files only. The kit's libraries name each other `@rpath/lib/…` and carry no rpath of their own: a
# program's rpath to `_fb` answers them, which is why every program and our top-level dylibs get one. The
# kit is Firebird's signed build and stays untouched.
while IFS= read -r -d '' f; do
  is_macho "$f" || continue
  rel="${f#$stage/}"
  case "$rel" in
    _fb/*)                  continue ;;
    *.app/Contents/MacOS/*) fix_rpath "$f" '@executable_path/../../..' '@executable_path/../../../_fb' ;;
    plugins/*)              fix_rpath "$f" '@loader_path/..' ;;
    *.dylib|*.so|*.bundle)  fix_rpath "$f" '@loader_path' '@loader_path/_fb' ;;
    *)                      fix_rpath "$f" '@executable_path' '@executable_path/_fb' ;;
  esac
  codesign --force --sign - "$f"
done < <(find "$stage" -type f -print0)

for app in "$stage"/*.app; do
  codesign --force --deep --sign - "$app"
done

required=(enterprise.app designer.app launcher.app codeRunner daemon libbackend.dylib libfrontend.dylib backend.conf help/en.hlk lang/ru/open_es.mo
          _fb/lib/libfbclient.dylib _fb/lib/libtommath.dylib _fb/plugins/libEngine13.dylib _fb/intl/libfbintl.dylib _fb/firebird.msg _fb/firebird.conf)
for r in "${required[@]}"; do
  [ -e "$stage/$r" ] || { echo "::error::The package would not run - missing: $r"; exit 1; }
done

# Every @rpath dependency must resolve, inside the package: ours through the file's own rpaths, the kit's
# through `_fb` (the rpath the programs give it); and every @loader_path one beside the file.
unresolved=0
while IFS= read -r -d '' f; do
  is_macho "$f" || continue
  dir="$(dirname "$f")"
  rel="${f#$stage/}"
  while IFS= read -r dep; do
    found=0
    case "$dep" in
      @loader_path/*)
        [ -e "$dir/${dep#@loader_path/}" ] && found=1 ;;
      @rpath/*)
        name="${dep#@rpath/}"
        case "$rel" in
          _fb/*) [ -e "$stage/_fb/$name" ] && found=1 ;;
          *)
            while IFS= read -r r; do
              r="${r//@executable_path/$dir}"
              r="${r//@loader_path/$dir}"
              if [ -e "$r/$name" ]; then found=1; break; fi
            done < <(rpaths_of "$f") ;;
        esac ;;
    esac
    if [ "$found" -eq 0 ]; then
      echo "::error::$rel cannot find $dep"
      unresolved=1
    fi
  done < <(otool -L "$f" | awk 'NR > 1 && ($1 ~ /^@rpath\// || $1 ~ /^@loader_path\//) {print $1}')
done < <(find "$stage" -type f -print0)
[ "$unresolved" -eq 0 ] || exit 1

# ⭐ THE KIT OPENS A BASE WHERE IT STANDS — see the same step in package-linux.sh. Firebird's own isql from
# the same package goes into the kit for a minute; its libraries are found through `_fb`, as the programs
# find them, and it is taken out again before the image is made.
smoke_dir="$(mktemp -d)"
mkdir -p "$stage/_fb/bin"
cp -a fb5pkg/Firebird.pkg/Payload/Versions/A/Resources/bin/isql "$stage/_fb/bin/isql"
if ! otool -l "$stage/_fb/bin/isql" | grep -q "@executable_path/.."; then
  install_name_tool -add_rpath '@executable_path/..' "$stage/_fb/bin/isql" 2>/dev/null || true
  codesign --force --sign - "$stage/_fb/bin/isql"
fi
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
  otool -L "$stage/_fb/bin/isql" || true
  otool -l "$stage/_fb/bin/isql" | grep -A2 LC_RPATH || true
  exit 1
fi
echo "Firebird kit: an embedded UTF8 base created, written and read in place."
rm -rf "$stage/_fb/bin" "$smoke_dir"

ln -s /Applications "$root/Applications"
rm -f "$out"
hdiutil create -volname "OES Nightly" -srcfolder "$root" -ov -format UDZO "$out"
ls -la "$out"
