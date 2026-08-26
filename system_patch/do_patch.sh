#!/bin/bash
# Copyright (c) 2025 Eclipse Oniro for OpenHarmony contributors.
# SPDX-License-Identifier: Apache-2.0
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Apply every .patch under ./patches/<repo_path>/ via `git am`, so each
# patch becomes a real commit in the target repository. Pair with
# undo_patch.sh to roll back.

set -e

PATCH_DIR="$(cd "$(dirname "$0")"; pwd)/patches"
SOURCE_ROOT_DIR="$(cd "$PATCH_DIR/../../../../../"; pwd)"

# Collect every directory that directly contains at least one .patch file,
# deduped and sorted so application order is deterministic.
mapfile -t repo_dirs < <(
    find "$PATCH_DIR" -type f -name "*.patch" -printf '%h\n' | sort -u
)

if [[ ${#repo_dirs[@]} -eq 0 ]]; then
    echo "No patches found under $PATCH_DIR"
    exit 0
fi

for dir in "${repo_dirs[@]}"; do
    repo_rel="$(realpath --relative-to="$PATCH_DIR" "$dir")"
    repo_path="$SOURCE_ROOT_DIR/$repo_rel"

    if [[ ! -d "$repo_path/.git" && ! -f "$repo_path/.git" ]]; then
        echo "ERROR: $repo_path is not a git repository" >&2
        exit 1
    fi

    mapfile -t patches < <(find "$dir" -maxdepth 1 -type f -name "*.patch" | sort)

    echo "Applying ${#patches[@]} patch(es) in $repo_rel"
    cd "$repo_path"
    for p in "${patches[@]}"; do
        echo "  git am $p"
        if ! git am "$p"; then
            git am --abort || true
            echo "ERROR: failed to apply $p in $repo_rel" >&2
            exit 1
        fi
    done
done
