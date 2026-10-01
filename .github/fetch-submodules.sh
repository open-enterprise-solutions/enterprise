#!/usr/bin/env bash
# Every submodule the build needs, fetched with patience - the only step every job of every workflow starts
# with. It NAMES NONE OF THEM: `git submodule update --init --recursive` takes whatever .gitmodules says, so
# a submodule added to the tree, moved or bumped is picked up here, and by the nightly, without anyone
# remembering to edit this file.
#
# GitHub answers a clone with HTTP 503 now and then, and git retries a failed submodule only once: on
# 2026-09-17 the macOS test job lost its whole run to `libwebp` (a submodule of wxWidgets) answering 503
# twice within a minute - nothing in the code, and nothing a person could do but press Re-run. So the fetch
# is tried again, with a longer wait each time, before the job gives up.
#
# docs/private is left out, and NOT by a list here: it carries `update = none` in .gitmodules, which is what
# a fetch naming no path obeys. It is a private submodule the workflow token cannot read, and a fetch that
# tried it would fail the job. Whoever may read it asks for it by hand, which overrides that line:
#     git submodule update --init --checkout docs/private
set -u

attempts=4
for attempt in $(seq 1 "$attempts"); do
  if git submodule update --init --recursive; then
    exit 0
  fi
  if [ "$attempt" -lt "$attempts" ]; then
    wait=$((attempt * 30))
    echo "::warning::the submodules could not be fetched (attempt $attempt of $attempts) - trying again in $wait s"
    sleep "$wait"
  fi
done

echo "::error::the submodules could not be fetched after $attempts attempts"
exit 1
