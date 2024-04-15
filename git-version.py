import subprocess
import filecmp, tempfile, shutil, os

# Thank you https://docs.platformio.org/en/latest/projectconf/section_env_build.html !

gitFail = False
try:
    subprocess.check_call(["git", "status"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
except:
    gitFail = True

if gitFail:
    rev = " (noGit)"
    url = " (noGit)"
else:
    branchname = (
        subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"])
        .strip()
        .decode("utf-8")
    )
    revision = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).strip().decode("utf-8")
    modified = subprocess.check_output(["git", "status", "-uno", "-s"]).strip().decode("utf-8")
    if modified:
        dirty = "-dirty"
    else:
        dirty = ""
    rev = "%s-%s%s" % (branchname, revision, dirty)
    try:
        url = subprocess.check_output(["git", "config", "--get", "remote.origin.url"]).strip().decode("utf-8")
    except:
        url = "None"

git_info = rev
git_url = url

provisional = "src/version.cxx"
final = "src/version.cpp"
with open(provisional, "w") as fp:
    fp.write('const char* git_info     = \"' + git_info + '\";\n')
    fp.write('const char* git_url      = \"' + git_url + '\";\n')

if not os.path.exists(final):
    # No version.cpp so rename version.cxx to version.cpp
    os.rename(provisional, final)
elif not filecmp.cmp(provisional, final):
    # version.cxx differs from version.cpp so get rid of the
    # old .cpp and rename .cxx to .cpp
    os.remove(final)
    os.rename(provisional, final)
else:
    # The existing version.cpp is the same as the new version.cxx
    # so we can just leave the old version.cpp in place and get
    # rid of version.cxx
    os.remove(provisional)

Import("env")

#print()
cmd = '$PYTHONEXE $UPLOADER --chip esp32s3 merge_bin --output $BUILD_DIR/merged-flash.bin --flash_mode dio --flash_size 8MB '
for image in env.get("FLASH_EXTRA_IMAGES", []):
    cmd += image[0] + " " + env.subst(image[1]) + " "
cmd += " 0x10000 $BUILD_DIR/firmware.bin 0x670000 $BUILD_DIR/littlefs.bin"
#print(cmd)
#print()
env.AddCustomTarget(
    "buildall",
    ["$BUILD_DIR/firmware.bin", "$BUILD_DIR/littlefs.bin"],
    cmd
    #"$PYTHONEXE $UPLOADER --chip esp32s3 merge_bin --output $BUILD_DIR/merged-flash.bin --flash_mode dio --flash_size 8MB 0x0000 $BUILD_DIR/bootloader.bin 0x8000 $BUILD_DIR/partitions.bin 0xe000 C:/Users/wmb/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin 0x10000 $BUILD_DIR/firmware.bin 0x670000 $BUILD_DIR/littlefs.bin"
)
