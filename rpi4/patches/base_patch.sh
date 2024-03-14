#!/bin/bash

# Copyright (c) 2024 Institute of Software, Chinese Academy of Sciences.
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

#base patch 复制官方内核驱动到对应目录
set -e

PROJECT_ROOT=$1
KERNEL_VERSION=$2
KERNEL_SRC_TMP_PATH=${PROJECT_ROOT}/out/KERNEL_OBJ/kernel/src_tmp/${KERNEL_VERSION}

cp -rf ${PROJECT_ROOT}/kernel/linux/linux-5.10/drivers/staging/hilog ${KERNEL_SRC_TMP_PATH}/drivers/staging/hilog
cp -rf ${PROJECT_ROOT}/kernel/linux/linux-5.10/drivers/staging/hievent ${KERNEL_SRC_TMP_PATH}/drivers/staging/hievent
sed -i '/endif # STAGING/i source "drivers/staging/hilog/Kconfig"' ${KERNEL_SRC_TMP_PATH}/drivers/staging/Kconfig
sed -i '/endif # STAGING/i\' ${KERNEL_SRC_TMP_PATH}/drivers/staging/Kconfig
sed -i '/endif # STAGING/i source "drivers/staging/hievent/Kconfig"' ${KERNEL_SRC_TMP_PATH}/drivers/staging/Kconfig