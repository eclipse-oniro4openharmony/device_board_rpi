#!/usr/bin/env python3

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

import sys
import os
import os.path
import subprocess
import multiprocessing
import shutil
import pathlib

kernel_dir = os.path.abspath('../../out/kernel/OBJ/linux-5.10')
script_dir = os.path.abspath(os.path.dirname(__file__))
bootimgsize = 128*1024*1024
output_dir = sys.argv[1]
input_dir = sys.argv[2]

def remove_file(name):
    try:
        os.unlink(name)
    except FileNotFoundError:
        return

def remove_directory(name):
    try:
        shutil.rmtree(name)
    except FileNotFoundError:
        pass

def make_boot_img():
    oldpwd = os.getcwd()
    os.chdir(output_dir)

    imagefile = 'images/boot.img'
    imagefile_tmp = imagefile + '.tmp'
    boot_dir = 'rpiboot'
    overlays_dir = 'rpiboot/overlays'
    remove_directory(boot_dir)
    remove_file(imagefile)
    shutil.copytree(input_dir, boot_dir)
    os.mkdir(overlays_dir)
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm64/boot/Image.gz'),
        boot_dir
    )
    shutil.copy(
        os.path.join(output_dir, 'images/ramdisk.img'),
        boot_dir
    )
    #设备树
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/bcm2711-rpi-4-b.dtb'),
        boot_dir
    )
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/bcm2710-rpi-3-b.dtb'),
        boot_dir
    )
    #GPU驱动
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/overlays/vc4-fkms-v3d.dtbo'),
        overlays_dir
    )
    #触摸屏驱动
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/overlays/rpi-ft5406.dtbo'),
        overlays_dir
    )
    #控制功耗和亮度
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/overlays/rpi-backlight.dtbo'),
        overlays_dir
    )
    #dwc
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/overlays/dwc2.dtbo'),
        overlays_dir
    )

    with open(imagefile_tmp, 'wb') as writer:
        writer.truncate(bootimgsize)
    subprocess.run(F'mkfs.vfat -F 32 {imagefile_tmp} -n BOOT', shell=True, check=True)
    subprocess.run(F'mcopy -i {imagefile_tmp} {boot_dir}/* ::/', shell=True, check=True)
    os.rename(imagefile_tmp, imagefile)

    os.chdir(oldpwd)

command_table = {
    'bootimg': make_boot_img,
}

command_table["bootimg"]()
