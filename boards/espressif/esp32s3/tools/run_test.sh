#!/usr/bin/env bash
# Build, flash, and run NSH stress test for the current esp32s3 board tree.
#
# Usage:
#   boards/espressif/esp32s3/tools/run_test.sh [--skip-build] [--skip-flash] [--skip-test]
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
BOARD_DIR="${ROOT}/boards/espressif/esp32s3"
CONFIG=espressif_esp32s3_default
SKIP_BUILD=0
SKIP_FLASH=0
SKIP_TEST=0

for arg in "$@"; do
	case "${arg}" in
	--skip-build) SKIP_BUILD=1 ;;
	--skip-flash) SKIP_FLASH=1 ;;
	--skip-test) SKIP_TEST=1 ;;
	*) echo "Unknown arg: ${arg}" >&2; exit 1 ;;
	esac
done

echo "=== ESP32-S3 build + flash + stress test ==="

if [[ "${SKIP_BUILD}" -eq 0 ]]; then
	echo "Building..."
	(cd "${ROOT}" && make "${CONFIG}")
fi

if [[ "${SKIP_FLASH}" -eq 0 ]]; then
	echo "Flashing..."
	bash "${BOARD_DIR}/tools/flash_firmware.sh"
fi

if [[ "${SKIP_TEST}" -eq 0 ]]; then
	python3 "${BOARD_DIR}/tools/stress_nsh.py"
else
	echo "Skipping stress test (--skip-test)"
fi
