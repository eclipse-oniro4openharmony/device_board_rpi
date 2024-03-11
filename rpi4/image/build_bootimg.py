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

os.environ['ARCH'] = 'arm64'
os.environ['CROSS_COMPILE'] = 'arm-linux-gnueabihf-'

defconfig = 'bcm2711_oh_defconfig'
kernel_dir = os.path.abspath('../../out/kernel/OBJ/linux-5.10')
script_dir = os.path.abspath(os.path.dirname(__file__))
bootimgsize = 128*1024*1024
command = sys.argv[1]
output_dir = sys.argv[2]

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

def make_kernel_all():
    oldpwd = os.getcwd()
    os.chdir(kernel_dir)

    zimage = os.path.join(output_dir, 'Image.gz')
    remove_file(zimage)
    nproc = multiprocessing.cpu_count()
    subprocess.run(F'make {defconfig}', shell=True, check=True)
    subprocess.run(F'make -j{nproc+1} Image.gz modules dtbs', shell=True, check=True)
    shutil.copy(
        os.path.join('arch/arm64/boot/Image.gz'),
        zimage
    )

    os.chdir(oldpwd)

def install_kernel_modules():
    modules_dir = os.path.join(output_dir, 'kernel_modules')
    remove_directory(modules_dir)
    kolist = [
        'drivers/net/wireless/broadcom/brcm80211/brcmfmac/brcmfmac.ko',
        'drivers/net/wireless/broadcom/brcm80211/brcmutil/brcmutil.ko',
    ]
    os.mkdir(modules_dir)
    for ko in kolist:
        shutil.copy(os.path.join(kernel_dir, ko), modules_dir)

def make_boot_img():
    oldpwd = os.getcwd()
    os.chdir(output_dir)

    imagefile = 'images/boot.img'
    imagefile_tmp = imagefile + '.tmp'
    boot_dir = 'rpi4boot'
    overlays_dir = 'rpi4boot/overlays'
    remove_directory(boot_dir)
    remove_file(imagefile)
    shutil.copytree(os.path.join(script_dir, 'rpi4boot'), boot_dir)
    # os.mkdir(overlays_dir)
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm64/boot/Image.gz'),
        boot_dir
    )
    shutil.copy(
        os.path.join(output_dir, 'images/ramdisk.img'),
        boot_dir
    )
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/bcm2711-rpi-4-b.dtb'),
        boot_dir
    )
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/overlays/vc4-fkms-v3d.dtbo'),
        overlays_dir
    )
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/overlays/rpi-ft5406.dtbo'),
        overlays_dir
    )
    shutil.copy(
        os.path.join(kernel_dir, 'arch/arm/boot/dts/overlays/rpi-backlight.dtbo'),
        overlays_dir
    )

    with open(imagefile_tmp, 'wb') as writer:
        writer.truncate(bootimgsize)
    subprocess.run(F'mkfs.vfat -F 32 {imagefile_tmp} -n BOOT', shell=True, check=True)
    subprocess.run(F'mcopy -i {imagefile_tmp} {boot_dir}/* ::/', shell=True, check=True)
    os.rename(imagefile_tmp, imagefile)

    os.chdir(oldpwd)

command_table = {
    'kernel': make_kernel_all,
    'modules': install_kernel_modules,
    'bootimg': make_boot_img,
}

command_table[command]()
