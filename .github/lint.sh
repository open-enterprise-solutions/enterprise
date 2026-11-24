#!/usr/bin/env bash
# The review remarks a script can make, made before a reviewer has to.
#
# Every rule here was first written by hand on a pull request, and more than once: a header guarded with
# `#pragma once`, a tool's attribution footer in a commit message, a new XPM icon, a `_()` message id with an
# em dash in it. None of them needs judgement, and each one cost a round trip: the author waited for a review
# to learn what a script could have said in seconds. So the script says it, in the words docs/development.md
# uses, on the line it is about.
#
# ⭐ ONLY WHAT THE CHANGE BRINGS. The rules are asked of the commits and the lines between <base> and <head>,
# never of the whole tree. Forty XPM files and a few attributed commits are already in history, and a check
# that fails on what the author did not write teaches them to stop reading it.
#
# Customer names are not checked here: the list would itself name them.
#
# Usage:  bash .github/lint.sh <base> [<head>]        e.g. before a push:  bash .github/lint.sh origin/develop
set -u

base=${1:?usage: bash .github/lint.sh <base> [<head>]}
head=${2:-HEAD}

for rev in "$base" "$head"; do
  if ! git rev-parse -q --verify "$rev^{commit}" >/dev/null; then
    echo "::error::lint: '$rev' is not a commit in this clone - a shallow checkout needs fetch-depth: 0"
    exit 2
  fi
done

# Code we carry but did not write answers to its own authors.
exclude=(':(exclude)src/3rdparty' ':(exclude,glob)src/engine/backend/databaseLayer/*/engine/**')

failures=0

# An annotation on the line in CI; file:line: text anywhere else.
report() {   # report <file> <line> <text>
  failures=$((failures + 1))
  if [ -n "${GITHUB_ACTIONS:-}" ]; then
    echo "::error file=$1,line=$2::${3//%/%25}"
  else
    echo "$1:$2: $3"
  fi
}

report_commit() {   # report_commit <sha> <text>
  failures=$((failures + 1))
  if [ -n "${GITHUB_ACTIONS:-}" ]; then
    echo "::error title=Commit ${1:0:8}::${2//%/%25}"
  else
    echo "commit ${1:0:8}: $2"
  fi
}

# 1. A commit message carries no generated footer and no tool attribution line (development.md section 1).
attribution='^[[:space:]]*co-authored-by:.*(claude|anthropic|openai|codex|chatgpt|copilot|cursor|gemini|devin|aider|windsurf|codeium)|^[^[:alnum:]]*generated (with|by) \[?(claude|codex|chatgpt|openai|copilot|cursor|gemini|aider|windsurf)|noreply@anthropic\.com'
for sha in $(git rev-list "$base..$head"); do
  line=$(git log -1 --format=%B "$sha" | grep -iE -m1 "$attribution")
  if [ -n "$line" ]; then
    report_commit "$sha" "carries a tool attribution line (\"$line\"). A commit message is English prose with no generated footers or attribution lines - reword it (git rebase -i, then reword) and push again. See docs/development.md section 1."
  fi
done

# 2. A header is guarded with #ifndef, never #pragma once (development.md section 2).
while IFS= read -r file; do
  [ -n "$file" ] || continue
  while IFS= read -r n; do
    report "$file" "$n" "#pragma once - a header here is guarded with #ifndef __NAME_H__ / #define __NAME_H__ ... #endif, like the headers around it. See docs/development.md section 2."
  done < <(git show "$head:$file" | perl -ne 'print "$.\n" if /^(?:\xEF\xBB\xBF)?\s*#\s*pragma\s+once\b/')
done < <(git -c core.quotePath=false diff --name-only -M --diff-filter=ACMR "$base...$head" -- '*.h' '*.hpp' "${exclude[@]}")

# 3. A new picture is SVG text in the source, never XPM (development.md section 2).
while IFS= read -r file; do
  [ -n "$file" ] || continue
  report "$file" 1 "a new XPM picture - a picture is SVG text in the file that reads it (static const ibString s_<name>_svg). See docs/development.md section 2."
done < <(git -c core.quotePath=false diff --name-only -M --diff-filter=AC "$base...$head" -- '*.xpm' "${exclude[@]}")

# 3a. Nor a PNG in Base64 (iVBORw0KGgo is a PNG's signature, encoded). Asked of the ADDED lines only.
while IFS=$'\t' read -r file n; do
  [ -n "$file" ] || continue
  report "$file" "$n" "a PNG in Base64 - a picture is SVG text in the file that reads it (static const ibString s_<name>_svg), drawn by the core at the size asked. See docs/development.md section 2."
done < <(git -c core.quotePath=false diff -U0 -M --no-color "$base...$head" -- '*.cpp' '*.h' '*.hpp' '*.inl' "${exclude[@]}" | perl -ne '
  if (/^\+\+\+ (?:b\/(.*)|\/dev\/null)$/) { $f = $1; next }
  if (/^@@ -\S+ \+(\d+)/)                { $n = $1; next }
  next unless defined $f && /^\+/;
  print "$f\t$n\n" if /iVBORw0KGgo/;
  $n++;
')

# 4. A _() message id is ASCII (portability.md section 1.9a). Asked of the ADDED lines only, and never of a
# comment: typography in a comment is harmless, and there is plenty of it.
while IFS=$'\t' read -r file n; do
  [ -n "$file" ] || continue
  report "$file" "$n" "a _() message id with a non-ASCII character - under the C locale CI runs in, it comes back EMPTY. Write the key in ASCII (- for an em dash, ... for an ellipsis, a plain quote for guillemets); typography belongs in the translation. See docs/portability.md section 1.9a."
done < <(git -c core.quotePath=false diff -U0 -M --no-color "$base...$head" -- '*.cpp' '*.h' '*.hpp' '*.inl' "${exclude[@]}" | perl -ne '
  if (/^\+\+\+ (?:b\/(.*)|\/dev\/null)$/) { $f = $1; next }
  if (/^@@ -\S+ \+(\d+)/)                { $n = $1; next }
  next unless defined $f && /^\+/;
  my $l = substr($_, 1);
  print "$f\t$n\n"
    if $l !~ m{^\s*(?://|/?\*)}
    && $l =~ m{^(?:(?!//).)*?(?<!\w)(?:_|wxTRANSLATE|wxPLURAL)\(\s*"(?:[^"\\\n]|\\.)*?[\x80-\xFF]};
  $n++;
')

# 4a. No \x escape above 0x7F in a string (portability.md section 1.9b). ibT() and ibMcpText() make a u"" literal, where
# \xNN is ONE UTF-16 unit and not a UTF-8 byte. Asked of the ADDED lines only, never of a comment.
while IFS=$'\t' read -r file n; do
  [ -n "$file" ] || continue
  report "$file" "$n" "a \\x escape above 0x7F in a string - ibT() and ibMcpText() make a u\"\" literal, where \\xF0\\x9F\\x9B\\x91 is four Latin-1 letters and not one emoji. Write the character itself (the sources are UTF-8), or \\u / \\U with its code point. See docs/portability.md section 1.9b."
done < <(git -c core.quotePath=false diff -U0 -M --no-color "$base...$head" -- '*.cpp' '*.h' '*.hpp' '*.inl' "${exclude[@]}" | perl -ne '
  if (/^\+\+\+ (?:b\/(.*)|\/dev\/null)$/) { $f = $1; next }
  if (/^@@ -\S+ \+(\d+)/)                { $n = $1; next }
  next unless defined $f && /^\+/;
  my $l = substr($_, 1);
  print "$f\t$n\n"
    if $l !~ m{^\s*(?://|/?\*)}
    && $l =~ m{^(?:(?!//).)*?"(?:[^"\\\n]|\\.)*?\\x[89A-Fa-f][0-9A-Fa-f]};
  $n++;
')

# 5. In backend/query/, a header of the metadata is included under provider/ and nowhere else (development.md
# section 2). Asked of an include the change BRINGS: one the same file already had - moved, re-spelled, its
# comment re-aligned - is not new, and the ones already there are being taken out one by one.
while IFS=$'\t' read -r file n; do
  [ -n "$file" ] || continue
  report "$file" "$n" "a metadata header included in backend/query/ outside provider/ - L3 reads a source through its queryable, and a metaobject is the provider's. Ask the queryable, or move the code that needs the metaobject to query/provider/. See docs/development.md section 2."
done < <(git -c core.quotePath=false diff -U0 -M --no-color "$base...$head" -- 'src/engine/backend/query/*' ':(exclude)src/engine/backend/query/provider/*' "${exclude[@]}" | perl -ne '
  if (/^\+\+\+ (?:b\/(.*)|\/dev\/null)$/) { $f = $1; next }
  if (/^@@ -\S+ \+(\d+)/)                { $n = $1; next }
  next unless defined $f;
  if (/^-\s*#\s*include\s*"([^"]+)"/)    { $had{"$f\t$1"} = 1; next }
  next unless /^\+/;
  push @added, [$f, $n, $1] if /^\+\s*#\s*include\s*"((?:backend\/)?(?:metaData\.h|metadata\w*\.h|metaCollection\/)[^"]*)"/;
  $n++;
  END { print "$$_[0]\t$$_[1]\n" for grep { !$had{"$$_[0]\t$$_[2]"} } @added }
')

# 6. The providers are the kitchen: nothing outside backend/query/provider/ includes from it (development.md section
# 2). A source is read through its queryable, a provider through the interface the face declares (queryProvider.h),
# the composer through the verbs the door declares. Named, with their reason: the metadata side (metaCollection/,
# metaData.cpp, metadataApplication* - it answers the database provider's questions and holds its structure
# builder) and the platform's jobs (job/platformJobs.cpp). An include is the kitchen's whichever way it is spelled - from
# the engine's root, from backend/, or relative inside query/. As in 5, an include the file already had - its header
# moved into the kitchen under the same name - is not new.
while IFS=$'\t' read -r file n; do
  [ -n "$file" ] || continue
  report "$file" "$n" "an include from backend/query/provider/ outside it - a provider is the kitchen: read a source through its queryable, a provider through query/queryProvider.h, the composer through the door. See docs/development.md section 2."
done < <(git -c core.quotePath=false diff -U0 -M --no-color "$base...$head" -- 'src/engine' ':(exclude)src/engine/backend/query/provider' ':(exclude)src/engine/backend/metaCollection' ':(exclude)src/engine/backend/metaData.cpp' ':(exclude,glob)src/engine/backend/metadataApplication*' ':(exclude)src/engine/backend/job/platformJobs.cpp' "${exclude[@]}" | perl -ne '
  if (/^\+\+\+ (?:b\/(.*)|\/dev\/null)$/) { $f = $1; next }
  if (/^@@ -\S+ \+(\d+)/)                { $n = $1; next }
  next unless defined $f;
  if (/^-\s*#\s*include\s*"(?:[^"]*\/)?([^"\/]+)"/) { $had{"$f\t$1"} = 1; next }
  next unless /^\+/;
  if (/^\+\s*#\s*include\s*"([^"]+)"/) {
    my $inc = $1;
    my $kitchen = $inc =~ m{(?:^|/)query/provider/} || ($f =~ m{^src/engine/backend/query/} && $inc =~ m{^(?:\.\./)*provider/});
    (my $base = $inc) =~ s{.*/}{};
    push @added, [$f, $n, $base] if $kitchen;
  }
  $n++;
  END { print "$$_[0]\t$$_[1]\n" for grep { !$had{"$$_[0]\t$$_[2]"} } @added }
')

range="$(git rev-parse --short "$base")..$(git rev-parse --short "$head")"
if [ "$failures" -gt 0 ]; then
  echo "lint: $failures finding(s) in $range"
  exit 1
fi
echo "lint: $range is clean"
