import subprocess, os, sys, shutil
import hashlib

relPath = os.path.join('release')

tag = (
    subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"])
    .strip()
    .decode("utf-8")
)

manifestRelPath = relPath

manifest = {
        "name": "FluidDial",
        "version": tag,
        "source_url": "https://github.com/bdring/FluidDial/tree/" + tag,
        "release_url": "https://github.com/bdring/FluidDial/releases/tag/" + tag,
        "funding_url": "https://www.paypal.com/donate/?hosted_button_id=8DYLB6ZYYDG7Y",
        "images": {},
        "files": {},
        # "upload": {
        #     "name": "upload",
        #     "description": "Things you can upload to the file system",
        #     "choice-name": "Upload group",
        #     "choices": []
        # },
        "installable": {
            "name": "installable",
            "description": "Things you can install",
            "choice-name": "Processor type",
            "choices": []
        },
}

def addImage(name, offset, filename, srcpath, dstpath):
    fulldstpath = os.path.join(manifestRelPath,os.path.normpath(dstpath))
    os.makedirs(fulldstpath, exist_ok=True)

    fulldstfile = os.path.join(fulldstpath, filename)
    reldstfile = os.path.join(dstpath, name) + ".bin"

    shutil.copy(os.path.join(srcpath, filename), fulldstfile)

    print("image", name)

    with open(fulldstfile, "rb") as f:
        data = f.read()
    image = {
        # "name": name,
        "size": os.path.getsize(fulldstfile),
        "offset": offset,
        "path": reldstfile,
        "signature": {
            "algorithm": "SHA2-256",
            "value": hashlib.sha256(data).hexdigest()
        }
    }
    if manifest['images'].get(name) != None:
        print("Duplicate image name", name)
        sys.exit(1)
    manifest['images'][name] = image

for envName in ['m5dial', 'cyddial']:
    buildDir = os.path.join('.pio', 'build', envName)
    addImage(envName, '0x0000', 'merged-flash.bin', buildDir, "")

installableChoices = manifest['installable']['choices']
def addSection(node, name, description, choice):
    section = {
        "name": name,
        "description": description,
    }
    if choice != None:
        section['choice-name'] = choice
        section['choices'] = []
    node.append(section)
    return section['choices']

mcuChoices = None
def addMCU(name, description, choice=None):
    global mcuChoices
    mcuChoices = addSection(installableChoices, name, description, choice)

dialChoices = None
def addDialType(name, description, choice=None):
    global dialChoices
    dialChoices = addSection(mcuChoices, name, description, choice)

def addInstallable(install_type, erase, images):
    for image in images:
        if manifest['images'].get(image) == None:
            print("Missing image", image)
            sys.exit(1)
                      
    installable = {
        "name": install_type["name"],
        "description": install_type["description"],
        "erase": erase,
        "images": images
    }
    dialChoices.append(installable)

fresh_install = { "name": "install", "description": "Complete FluidDial installation"}

def makeManifest():
    mcu = "esp32"
    addMCU(mcu, "ESP32-WROOM", "Firmware variant")

    addDialType("FluidDial for CYD", "FluidDial for CYD Dial", "FluidDial type")
    addInstallable(fresh_install, True, ["cyddial"])

    mcu = "esp32s3"
    addMCU(mcu, "ESP32-S3-WROOM-1", "Firmware variant")

    addDialType("FluidDial for M5Dial", "FluidDial for M5Dial", "FluidDial type")
    addInstallable(fresh_install, True, ["m5dial"])

makeManifest()

import json
def printManifest():
    print(json.dumps(manifest, indent=2))

with open(os.path.join(manifestRelPath, "manifest.json"), "w") as manifest_file:
    json.dump(manifest, manifest_file, indent=2)
                 
sys.exit(0)
