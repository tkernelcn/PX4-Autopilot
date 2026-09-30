# 一级中断返回时用错上下文指针导致卡死

目标板 `espressif_esp32s3_default`。系统跑十几到几十分钟后控制台不再响应，LED 停在最后一次的亮灭状态。根因是一级中断处理函数在“已经没有待处理中断”时直接返回，把被打断任务的 `a2` 当成了寄存器保存区地址。

| 项目 | 值 |
|------|-----|
| 现象 | 串口死、LED 冻住，复位后恢复 |
| 出现时机 | 开机后即可发生，平均要十几到几十分钟才碰上 |
| 异常 | `EXCCAUSE 28` LoadProhibited，读地址 `0x69` |
| 出错点 | `_xtensa_context_restore` 恢复浮点寄存器 |
| 被打断任务 | `mavlink_if0`，pid 987 |
| 修改文件 | `platforms/nuttx/NuttX/nuttx/arch/xtensa/src/common/xtensa_int_handlers.S` |
| 抓取脚本 | `boards/espressif/esp32s3/tools/capture_hang.sh` |

---

## 1. 现象

板子上电后传感器、姿态估计、串口都正常。持续运行一段时间后同时出现：

- NSH 不再回显，已打开的串口会话没有新输出。
- 板载 LED 停在卡死前的那一状态，不再翻转。
- 不是某个外设单独停住。复位后系统又能正常起来，直到下一次再卡死。

LED 由 commander 翻转。卡死后 `xtensa_assert()` 先 `up_irq_save()` 再 `for (;;)` 空转。中断被关掉，串口任务和 LED 任务都得不到执行，所以两者一起停。`CONFIG_ARCH_LEDS` 未打开时，这条 panic 路径也不会闪灯。

---

## 2. 出现时机

开机第一分钟和跑了四十分钟用的是同一段代码。没有计数器、内存水位或超时在到期后才触发。

一级中断（定时器、串口、SPI 等）一直在进。处理函数入口会再读一次：

```text
INTENABLE & INTERRUPT & 本级掩码
```

结果为 0 时，旧代码认为“没有中断要分发”，直接去做上下文恢复。这时要同时满足下面三条才会读到非法地址：

1. 中断向量已经进了 `_xtensa_level1_handler`，但读出来的待处理位是 0。中断源已经撤掉，或这一位不属于一级掩码，都会走到这里。
2. 被打断任务当时的 `a2` 是一个很小的整数。`a2` 在 C 调用里常作参数或返回值，等于 1 只是其中一种。
3. `CPENABLE` 非 0，也就是这个任务还占着浮点协处理器。恢复路径才会按 `a2` 去加载浮点保存区。

三条对不上时，这条返回路径照样执行：`a2` 碰巧像一个可读地址，或者浮点关着、恢复直接跳过，系统就继续跑。`mavlink_if0` 一直在跑，一级中断也一直在进，这个组合平均要十几到几十分钟才碰上一次。

---

## 3. 抓到的现场

硬件断点停在 `xtensa_user_panic` 入口，panic 打印还没覆盖异常帧。CPU0 是出错核，CPU1 停在 ROM `0x40043a40`，`debugcause = 0x20`，是被 CPU0 的断点连带停住的。

| 项 | 值 |
|----|-----|
| 任务 | `mavlink_if0`，pid 987 |
| `PC` | `0x403794c0`，`xtensa_user_panic`（断点地址） |
| `debugcause` | `0x2`，指令断点 |
| `EXCCAUSE` | `28`，LoadProhibited |
| `EXCVADDR` | `0x00000069` |
| 故障 `EPC` | `0x40378acb` |
| 故障指令 | `l32i.n a4, a3, 0` |
| 所在函数 | `_xtensa_context_restore` |
| 返回地址 `A0` | `0x403785cb`，`_xtensa_level1_handler` 里 `call0 _xtensa_context_restore` 的下一条 |
| 传入的 `a2` | `1` |
| `a3` | `0x69` |
| `a8` / `CPENABLE` | `1`（只打开了 CP0，即浮点） |
| `a12` | `0x3fcb4f98`，真实的寄存器保存区 |
| `a13` | `_xtensa_coproc_saoffsets` |
| `a14` | `0`，CP0 保存区偏移 |

`_xtensa_context_restore` 约定 `a2` 是寄存器保存区。浮点保存区在保存区之后 `4 * XCPTCONTEXT_REGS = 104` 字节。当时的计算是：

```text
a3 = a2 + 104 + saoffsets[0]
   = 1 + 104 + 0
   = 105
   = 0x69
```

`0x69` 不允许加载，于是 LoadProhibited。真正的保存区一直在 `a12 = 0x3fcb4f98`，空返回路径没有把它写回 `a2`。

异常帧在中断栈上（`0x3fc92a48`，距 8 KB 中断栈顶大约 424 字节）。这不是中断栈溢出。帧里的 `EXCCAUSE`、`EXCVADDR` 与特殊寄存器一致，说明停住时帧还没被 panic 打印盖掉。

---

## 4. 原因

`dispatch_c_isr` 在 `xtensa_int_handlers.S` 里。有中断要分发时，它调用 `xtensa_int_decode`，把返回的保存区指针放进 `a2`，再 `call0 _xtensa_context_restore`。没有待处理位时，`beqz` 跳过这次调用。

跳过之后 `a2` 仍是被打断任务的原值。`_xtensa_context_restore` 以及它里面的 `xtensa_coproc_restorestate` 都把 `a2` 当作保存区基址。`CPENABLE == 1` 时，第一条浮点加载就是：

```text
40378acb:  l32i.n  a4, a3, 0
```

`a2` 为 1 时，这条指令访问 `0x69`。用户异常进入 `xtensa_user_panic`，再进 `xtensa_assert`，关中断后空转。串口和 LED 因此一起停。

`a12` 在进入 `dispatch_c_isr` 之前已经保存了当前栈上的寄存器帧，并且在这条路径上没有被改掉。空返回时应把 `a2` 设回 `a12`。

---

## 5. 抓取方法

卡死要等很久，而且 panic 打印会把异常帧盖掉。事后再挂调试器只能看到空转，看不到 `EXCVADDR`。

### 5.1 事后抓取为什么不够

第一次在已经卡死时用 GDB 附加，停在 `xtensa_assert` 的 `for (;;)`。函数参数里能看到 `exccause = 28`，但保存在中断栈上的帧已经被 `xtensa_dumpstate()` 写坏：`EXCVADDR` 槽里是打印函数的指针，不能当作出错地址。回溯最外层的 `0x40040023` 是异常返回时用 `EPC` 拼出来的地址，落在 WiFi ROM 的数据上，不是调用点。

`xtensa_dumpstate()` 和断言空转都跑在同一块中断栈上，从栈顶往下长，最终覆盖异常帧靠近栈顶的部分。必须在 `xtensa_user_panic` 入口、打印之前停住。

### 5.2 可用的抓法

脚本 `boards/espressif/esp32s3/tools/capture_hang.sh` 用 OpenOCD 的 telnet 口下硬件断点，不经过 GDB 的 `continue`。

GDB `continue` 在这颗双核上会把两颗核再次停住，LED 立刻不再闪，板子并没有继续跑。OpenOCD 默认按 FreeRTOS 枚举线程，这块板子是 NuttX，恢复时连接会被拆掉。脚本因此使用 `set ESP_RTOS none`。

步骤：

1. 固件与 ELF 必须是同一次编译。ELF 默认是 `build/espressif_esp32s3_default/espressif_esp32s3_default.elf`。
2. 复位，等板子正常跑起来，LED 在闪。
3. 退出 picocom 或 minicom。USB Serial/JTAG 与 NSH 共用同一条 USB，串口开着时 OpenOCD 会失败。
4. 执行：

```bash
./boards/espressif/esp32s3/tools/capture_hang.sh --wait
```

5. 看到「已继续运行，小灯应重新闪」后，不要复位，不要再打开串口。断点在复位后会丢失。
6. 脚本大约每分钟打印一次已等待的时间。两颗核没有恢复运行时，脚本会直接退出，不会空等。
7. 再次卡死时脚本自己停住，日志在 `/tmp/esp32s3_hang.log`。OpenOCD 日志在 `/tmp/esp32s3_openocd.log`。

已经卡死、只看当前空转现场时用 `--now`。那种现场里的 `EXCVADDR` 通常已经无效。

脚本在 `xtensa_user_panic` 和 `xtensa_panic` 上下硬件断点，确认两颗核都是 `running` 之后才开始等。命中后读出 `pc`、`a0`–`a3`、`exccause`、`excvaddr`、`epc1`，再按 `a3` 指向的异常帧解出 `EPC`、`EXCCAUSE`、`EXCVADDR`，并用 `xtensa-esp32s3-elf-addr2line` 对上源码行。

---

## 6. 修改

`dispatch_c_isr` 在没有待处理中断时，把 `a2` 设回 `a12`：

```asm
	mov		a2, RETVAL
	j		2f

1:
	mov		a2, a12
2:
```

有中断要分发时，`a2` 仍是 `xtensa_int_decode` 的返回值（可能换到新任务的保存区）。没有中断时，`a2` 用进入本函数前就放在 `a12` 里的那份帧。`_xtensa_context_restore` 之后按这个指针恢复整数寄存器和浮点寄存器。

修改位置：`platforms/nuttx/NuttX/nuttx/arch/xtensa/src/common/xtensa_int_handlers.S` 的 `dispatch_c_isr`。各级中断共用这个宏。

### 6.1 重新编译

PX4 的 Ninja 规则原先只把 NuttX 的 `.c`、`.h` 算作依赖，改 `.S` 后再执行 `make espressif_esp32s3_default` 会显示 `ninja: no work to do.`。`platforms/nuttx/NuttX/CMakeLists.txt` 里对应的文件列表已加上 `.S` 和 `.s`。改过汇编后需要让 CMake 重新生成一次构建文件，再编译。

编译和烧录仍用原来的命令。烧录不要整片擦除，参数分区在 `0x310000`。

```bash
export PATH=~/toolchain/xtensa-esp-elf-gcc/bin:$PATH
make espressif_esp32s3_default
./boards/espressif/esp32s3/tools/flash_firmware.sh
```

---

## 7. 结果

- 卡死是一次非法加载，不是 SPI 忙等，也不是中断栈溢出。
- 出错地址 `0x69` 由上下文指针 `1` 加上浮点区偏移 `104` 得到。
- 真实保存区在 `a12`。空的一级中断返回路径没有使用它。
- 修复后，这条路径把 `a2` 设回 `a12`，再恢复现场。
- 若新固件再次卡死，用第 5 节的 `--wait` 重新抓。停住时的 `EPC` 若仍是 `_xtensa_context_restore` 且 `a2` 仍不是 `a12`，说明这条路径没有进到新固件。

---

## 8. 排查中排除的方向

这些都针对同一次“跑一段时间后串口和 LED 一起停”的现象，现场否定了它们。

| 方向 | 结论 |
|------|------|
| SPI2 轮询 `SPI_USR` 死循环 | 加过超时并烧录，长时间运行仍卡死。超时已撤掉，`esp32s3_spi.c` 回到原来的忙等。 |
| 降低 SPI2 工作队列优先级 | 试过并撤回。Kconfig 默认相对优先级 `-3`，绝对优先级 252。 |
| 打开 SPI2 DMA | 原地收发共用一个缓冲区，打开后 BMI088 无法工作。保持关闭。 |
| 提高 NSH 优先级 | 试过并撤回。NSH 仍是 100。卡死时中断已被关掉，提高线程优先级不会让控制台恢复。 |
| 中断栈溢出 | 帧在 8 KB 中断栈顶部附近，只占用约 424 字节。 |
| 堆耗尽 | 卡死形态是关中断空转，不是内存分配失败。内部堆当时大约用了一半。 |
