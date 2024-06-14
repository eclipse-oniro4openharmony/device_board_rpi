#!/bin/bash
# Copyright (c) 2022 Diemit <598757652@qq.com>
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

set -e

PROJECT_ROOT=$(pwd)/
PATCH_SRC_PATH=${PROJECT_ROOT}device/board/rpi/system_patch

# #chmod
chmod 777 ${PROJECT_ROOT}device/board/rpi/common/build_bootimg.py
chmod 777 ${PROJECT_ROOT}device/board/rpi/common/make_rpi_sdcard_image.py
chmod 777 ${PROJECT_ROOT}device/board/rpi/common/kernel/build_kernel.sh
chmod 777 ${PROJECT_ROOT}device/board/rpi/common/kernel/check_patch.sh
chmod 777 ${PROJECT_ROOT}device/board/rpi/common/kernel/make_kernel.sh
chmod 777 ${PROJECT_ROOT}device/board/rpi/common/kernel/make_dtb.mk
chmod 777 ${PROJECT_ROOT}device/board/rpi/common/kernel/make_kernel_32.mk
chmod 777 ${PROJECT_ROOT}device/board/rpi/common/kernel/make_kernel_64.mk

#whitelist
cp -arfL ${PATCH_SRC_PATH}/whitelist/compile_standard_whitelist.json ${PROJECT_ROOT}build/compile_standard_whitelist.json

#graphic_2d
cp -arfL ${PATCH_SRC_PATH}/graphic_2d/rs_base_render_engine.cpp ${PROJECT_ROOT}foundation/graphic/graphic_2d/rosen/modules/render_service/core/pipeline/rs_base_render_engine.cpp
cp -arfL ${PATCH_SRC_PATH}/graphic_2d/rs_draw_cmd.cpp ${PROJECT_ROOT}foundation/graphic/graphic_2d/rosen/modules/render_service_base/src/pipeline/rs_draw_cmd.cpp
cp -arfL ${PATCH_SRC_PATH}/graphic_2d/surface_image.cpp ${PROJECT_ROOT}foundation/graphic/graphic_2d/frameworks/surfaceimage/src/surface_image.cpp