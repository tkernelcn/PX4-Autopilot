# ESP32-S3 OpenOCD 调试（抓 crash PC / 回溯）

板载 **USB Serial/JTAG**（`303a:1001`）可同时做串口和 JTAG，无需外接调试器。

## 1. 安装（已完成）

Espressif OpenOCD 位于：

```
/home/gina/test/test_PX4/tools/openocd-esp32/
```

## 2. USB 权限（必须，一次性）

OpenOCD 走 **libusb**，与 `dialout` 串口权限不同。首次需：

```bash
# 推荐：udev 规则
sudo cp /home/gina/test/test_PX4/tools/openocd-esp32/99-espressif-usb-jtag.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
# 重新插拔 USB 或复位板子

# WSL 临时（每次插拔后 bus/dev 可能变）：
lsusb   # 找到 303a:1001
ls -l /dev/bus/usb/*/*
sudo chmod a+rw /dev/bus/usb/001/020   # 按实际路径改
```

## 3. 抓 crash（自动化）

先编译并烧录要调试的固件（如 minboot B7），**关闭 picocom**：

```bash
make espressif_esp32s3_default
boards/espressif/esp32s3/tools/flash_firmware.sh

ELF=build/espressif_esp32s3_default/espressif_esp32s3_default.elf \
  boards/espressif/esp32s3/tools/debug_crash.sh
```

输出：`/tmp/esp32s3_gdb_crash.log`（PC、backtrace、反汇编）

## 4. 交互调试

```bash
boards/espressif/esp32s3/tools/debug_gdb.sh
```

常用 GDB 命令：

```
monitor reset halt
continue
bt full
info registers
x/16i $pc
```

## 5. 与串口的关系

OpenOCD 占用 JTAG 时，`/dev/ttyACM0` 上的 NSH 可能不可用。调试结束 kill OpenOCD 后串口恢复。

## 6. 建议断点（USB 静默问题）

| 符号 | 含义 |
|------|------|
| `__esp32s3_start` | NuttX 早期启动 |
| `nx_start` | 内核 |
| `board_app_initialize` | PX4 board init |
| `px4_platform_init` | param/uorb/work_queue |
| `nsh_main` | NSH 入口 |
| `xtensa_user_panic` | 断言/panic |

若 halt 在 `xtensa_assert.c` 或 `EXCCAUSE` 非 0，记下 PC 并对照：

```bash
xtensa-esp32s3-elf-addr2line -e build/.../espressif_esp32s3_default.elf 0x403794ab
```
