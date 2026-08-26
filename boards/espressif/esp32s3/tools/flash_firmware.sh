#!/usr/bin/env bash
# 烧写 Bootloader(0x0) + 分区表(0x8000) + PX4 应用(0x10000)
#
# 单行（子 shell 执行，烧完自动回到当前目录）:
# ( cd ~/test/test_PX4/PX4-Autopilot && esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 write-flash --flash-freq 80m --flash-size 4MB 0x0 boards/espressif/esp32s3/boot/bootloader-esp32s3.bin 0x8000 boards/espressif/esp32s3/boot/partition-table-esp32s3.bin 0x10000 build/espressif_esp32s3_default/espressif_esp32s3_default.bin )
#
# 不要加 --flash-mode：esptool 会改写这一条命令里所有 bin 的镜像头。
# bootloader 预编译包已是 DIO（ROM 启动要求）；app 由 elf2image 按
# defconfig 打成 QIO（二级 bootloader 认片后再切 Quad）。

set -euo pipefail

PORT="${PORT:-/dev/ttyACM0}"
BAUD="${BAUD:-921600}"

BOOT_DIR="$(cd "$(dirname "$0")/../boot" && pwd)"
REPO_ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
APP_BIN="${REPO_ROOT}/build/espressif_esp32s3_default/espressif_esp32s3_default.bin"

# 首次烧写可取消下一行注释（会擦除整片 Flash，含参数区）
# esptool --chip esp32s3 -p "${PORT}" erase-flash

esptool --chip esp32s3 -p "${PORT}" -b "${BAUD}" write-flash \
  --flash-freq 80m --flash-size 4MB \
  0x0     "${BOOT_DIR}/bootloader-esp32s3.bin" \
  0x8000  "${BOOT_DIR}/partition-table-esp32s3.bin" \
  0x10000 "${APP_BIN}"
