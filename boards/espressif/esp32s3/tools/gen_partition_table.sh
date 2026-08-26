#!/usr/bin/env bash
# 从 partitions.csv 生成分区表 -> ../boot/partition-table-esp32s3.bin

BOOT_DIR="$(cd "$(dirname "$0")/../boot" && pwd)"
IDF_PATH="${HOME}/esp/esp-idf"
GEN="${IDF_PATH}/components/partition_table/gen_esp32part.py"

python3 "${GEN}" "${BOOT_DIR}/partitions.csv" "${BOOT_DIR}/partition-table-esp32s3.bin"
