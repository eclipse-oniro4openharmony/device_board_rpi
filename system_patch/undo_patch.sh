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
# Roll back the commits created by do_patch.sh. For each ./patches/<repo>/
# subdirectory, this runs `git reset --hard HEAD~N` in that repo, where N
# is the number of .patch files in the subdirectory.
#
# WARNING: this is destructive. If any commits were made on top of the
# patches after do_patch.sh ran, they will be discarded too. Verify each
# repo's top commits look like the applied patches before running.

set -e

PATCH_DIR="$(cd "$(dirname "$0")"; pwd)/patches"
SOURCE_ROOT_DIR="$(cd "$PATCH_DIR/../../../../../"; pwd)"

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

    count=$(find "$dir" -maxdepth 1 -type f -name "*.patch" | wc -l)

    echo "Reverting last $count commit(s) in $repo_rel"
    cd "$repo_path"
    git reset --hard "HEAD~${count}"
done
