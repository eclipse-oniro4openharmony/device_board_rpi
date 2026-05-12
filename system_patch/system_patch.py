#! /usr/bin/python
import os
import os.path
import json
PATCH_SRC_PATH=os.path.abspath(os.path.dirname(__file__))
PROJECT_ROOT=os.path.abspath(PATCH_SRC_PATH + '/../../../../')

def whitelist():
    whitelist_path=PROJECT_ROOT+'/build/compile_standard_whitelist.json'

    with open(whitelist_path, 'r') as file:
        data = json.load(file)

    addlist=["//device/soc/broadcom/bcm2711/hardware/display:display_gfx",
        "//device/soc/broadcom/bcm2711/hardware/display:libdisplay_buffer_vdi_impl",
        "//device/soc/broadcom/bcm2711/hardware/display:libdisplay_composer_vdi_impl",
        "//device/soc/broadcom/common:ptgen",
        "//device/board/rpi/common/camera/pipeline_core:camera_pipeline_core",
        "//device/board/rpi/common/camera:camera_host_vdi_impl_1.0"]

    for add in addlist:
        data["gn_part_or_subsystem_error"].append(add)
        data["deps_added_external_part_module"].append(add)
        data["external_deps_bundle_not_add"].append(add)
        data["third_deps_bundle_not_add"].append(add)

    with open(whitelist_path, 'w') as file:
        json.dump(data, file, indent=4)

whitelist()

print("finish")
