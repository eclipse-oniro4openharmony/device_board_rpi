#!/bin/bash -ex

if [ "X${SKIP_SYNC}" = "Xtrue" ] ; then
    exit 0
fi

dir_parent=$(cd $(dirname "${0}"); pwd)
PROJECT_ROOT=$(cd $(dirname "${dir_parent}/../../../../../"); pwd)

########### patches applied after this line ############
function git_am(){
    local dir_project=${1}
    local name_patch="${2}"
    cd "${PROJECT_ROOT}/${dir_project}"
    git am "${dir_parent}/${name_patch}" || (git am --abort; exit 1)
    cd -
}

git_am "base/startup/appspawn" "0001-appspawn_adapter.cpp-fix-unknown-type-name-error.patch"
git_am "foundation/ability/ability_runtime" "0001-main_thread.cpp-use-RPi4-version-SetCacheDir.patch"
git_am "foundation/bundlemanager/app_domain_verify" "0001-fix-for-foundation-crash-on-6.1.patch"
git_am "kernel/linux/linux-5.10" "0001-Revert-KVM-arm64-Don-t-retire-aborted-MMIO-instructi.patch"
git_am "third_party/toybox" "0001-BUILD.gn-workaround-for-error-misleading-indentation.patch"
