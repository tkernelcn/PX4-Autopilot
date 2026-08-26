#!/usr/bin/env bash
# Source before openocd/gdb:  source boards/espressif/esp32s3/tools/openocd_env.sh
OPENOCD_ROOT="${OPENOCD_ROOT:-/home/gina/test/test_PX4/tools/openocd-esp32}"
export OPENOCD_SCRIPTS="${OPENOCD_ROOT}/share/openocd/scripts"
export PATH="${OPENOCD_ROOT}/bin:${PATH}"
export OPENOCD="${OPENOCD_ROOT}/bin/openocd"
# Must use Espressif xtensa gdb (gdb-multiarch cannot parse esp32 target xml)
if [[ -x "${HOME}/.espressif/tools/xtensa-esp-elf-gdb/17.1_20260402/xtensa-esp-elf-gdb/bin/xtensa-esp32s3-elf-gdb" ]]; then
	export GDB="${GDB:-${HOME}/.espressif/tools/xtensa-esp-elf-gdb/17.1_20260402/xtensa-esp-elf-gdb/bin/xtensa-esp32s3-elf-gdb}"
else
	export GDB="${GDB:-gdb-multiarch}"
fi
