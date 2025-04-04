# This script adds a "build_merged" target, used like "pio run -t build_merged",
# that will create a combined image with both the firmware and the filesystem images
# The two smaller images must be built first, with "pio run" and "pio run -t buildfs"
Import("env")

cmd = '$PYTHONEXE $UPLOADER --chip esp32s3 merge_bin --output $BUILD_DIR/merged-flash.bin --flash_mode dio --flash_size 8MB '
for image in env.get("FLASH_EXTRA_IMAGES", []):
    cmd += image[0] + " " + env.subst(image[1]) + " "
cmd += " 0x10000 $BUILD_DIR/firmware.bin 0x670000 $BUILD_DIR/littlefs.bin"
#print(cmd)
#print()
env.AddCustomTarget(
    "build_merged",
    ["$BUILD_DIR/firmware.bin", "$BUILD_DIR/littlefs.bin"],
    cmd
    #"$PYTHONEXE $UPLOADER --chip esp32s3 merge_bin --output $BUILD_DIR/merged-flash.bin --flash_mode dio --flash_size 8MB 0x0000 $BUILD_DIR/bootloader.bin 0x8000 $BUILD_DIR/partitions.bin 0xe000 C:/Users/wmb/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin 0x10000 $BUILD_DIR/firmware.bin 0x670000 $BUILD_DIR/littlefs.bin"
)
