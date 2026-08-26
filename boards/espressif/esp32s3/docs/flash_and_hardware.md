# ESP32-S3 PX4 板级说明

本文档说明 `espressif_esp32s3_default` 目标的 **固件烧写**、**Flash 分区布局** 以及 **当前硬件接口与传感器配置**。

| 项目 | 值 |
|------|-----|
| 构建目标 | `make espressif_esp32s3_default` |
| 工具链 | `xtensa-esp32s3-elf` |
| 芯片型号 | ESP32-S3-WROOM-1-N4（4 MB Flash） |
| NuttX 应用格式 | Legacy（`CONFIG_ESP32S3_APP_FORMAT_LEGACY=y`） |
| 固件产物目录 | `build/espressif_esp32s3_default/` |
| 应用二进制 | `espressif_esp32s3_default.bin` |
| PX4 打包文件 | `espressif_esp32s3_default.px4`（**不能**用 QGC 升级，见下文） |
| `board_id` | **888**（`firmware.prototype`，勿与官方 88 号板冲突） |

---

## 1. 固件烧写

### 1.1 启动链说明

ESP32-S3 **不是** STM32 FMU 那种「PX4 Bootloader + 应用」模式，而是 Espressif 标准启动链：

```text
片内 ROM Bootloader（不可烧录、芯片自带）
        ↓
二级 Bootloader（Flash 0x000000）
        ↓
分区表（Flash 0x008000）
        ↓
NuttX / PX4 应用（Flash 0x010000）
```

因此：

- **空片或从未烧过 NuttX/ESP-IDF 固件**：不能只烧 `.bin`，必须先烧 Bootloader 和分区表。
- **Bootloader 与分区表已存在**：日常开发可只更新应用 `.bin`（烧到 `0x10000`）。

> **注意**：`.bin` 是 **应用镜像**，由 `esptool elf2image` 从 ELF 生成，**不是**从 Flash 地址 `0x0` 开始的整片镜像。  
> 链接/运行地址（如 IROM `0x42000020`）是 CPU 映射地址，与 `write_flash` 物理偏移不同。

### 1.2 Flash 分区布局（本板使用 2MB factory）

上游 [esp-nuttx-bootloader](https://github.com/espressif/esp-nuttx-bootloader) 默认 `factory` 只有 **1MB**。本飞控镜像约 **1.6~1.7MB**，必须使用本仓库的 **2MB factory** 分区表。

板级定义：`boot/partitions.csv`

| Flash 物理地址 | 大小 | 内容 | 来源 |
|----------------|------|------|------|
| `0x000000` | ~28 KB | 二级 Bootloader | [esp-nuttx-bootloader](https://github.com/espressif/esp-nuttx-bootloader) |
| `0x008000` | 4 KB | 分区表（**本板 2MB factory**） | 由 `boot/partitions.csv` 生成，**不要**用上游 1MB 默认表 |
| `0x009000` | 24 KB | NVS（IDF 惯例） | 分区表项 |
| `0x00F000` | 4 KB | phy_init | 分区表项 |
| `0x010000` | **2 MB** | factory 应用 | `espressif_esp32s3_default.bin` |
| `0x210000` ~ `0x30FFFF` | 约 1 MB | 未用间隙 | — |
| `0x310000` | 64 KB | 参数 MTD `/fs/mtd_params` | `init.c` 的 `BOARD_MTD_PARAMS_OFFSET/SIZE`，运行时读写 |

```text
0x000000  Bootloader
0x008000  分区表
0x010000  factory 应用（2M，至 0x210000）
0x310000  参数 MTD（64KB）
```

`factory` 不可扩到 3MB：会与 `0x310000` 参数区重叠。

### 1.3 获取 Bootloader 与分区表

Bootloader **不由 PX4 工程编译**，来自 Espressif 维护的预编译包。

#### 方式 A：下载 Bootloader + 用本仓库 CSV 生成分区表（推荐）

```bash
mkdir -p ~/esp-nuttx-bootloader && cd ~/esp-nuttx-bootloader
curl -LO https://github.com/espressif/esp-nuttx-bootloader/releases/download/latest/bootloader-esp32s3.bin

# 需要 ESP-IDF 的 gen_esp32part.py（或 pip 包 / IDF 组件）
python3 $IDF_PATH/components/partition_table/gen_esp32part.py \
  /path/to/PX4-Autopilot/boards/espressif/esp32s3/boot/partitions.csv \
  partition-table-esp32s3.bin
```

| 文件 | 烧写地址 |
|------|----------|
| `bootloader-esp32s3.bin` | `0x0` |
| **本板生成的** `partition-table-esp32s3.bin` | `0x8000` |

> 上游 `releases/latest/partition-table-esp32s3.bin` 是 **1MB factory**，与当前固件不兼容，不要直接用。

#### 方式 B：NuttX `make bootloader`

会下载 **默认 1MB** 分区表。若走这条路径，仍须用上面生成的 2MB 表覆盖 `partition-table-esp32s3.bin`。

#### 方式 C：从源码构建 boot

克隆 [esp-nuttx-bootloader](https://github.com/espressif/esp-nuttx-bootloader)，把本板 `boot/partitions.csv` 拷进去再 `build_idfboot.sh -c esp32s3`。

### 1.4 烧写地址对照表

| 文件 | `esptool write_flash` 地址 | 说明 |
|------|---------------------------|------|
| `bootloader-esp32s3.bin` | `0x0` | 仅首次或更换 boot 时需要 |
| 本板 `partition-table-esp32s3.bin` | `0x8000` | 仅首次或修改分区表时需要 |
| `espressif_esp32s3_default.bin` | `0x10000` | 应用固件，日常更新 |

### 1.5 烧写命令

```bash
pip install esptool
```

将 `<PORT>` 换成实际串口（如 `/dev/ttyACM0`）。进入下载模式：按住 **BOOT**，点 **RESET**，松开 **BOOT**。

```bash
# 首次建议全片擦除（会清掉 0x310000 参数）
esptool --chip esp32s3 -p <PORT> erase-flash

# 不要加 --flash-mode：会改写 bootloader 和应用的镜像头。
# bootloader 保持 DIO（ROM 启动）；app 由编译打成 QIO。
esptool --chip esp32s3 -p <PORT> -b 921600 write-flash \
  --flash-freq 80m --flash-size 4MB \
  0x0     boards/espressif/esp32s3/boot/bootloader-esp32s3.bin \
  0x8000  boards/espressif/esp32s3/boot/partition-table-esp32s3.bin \
  0x10000 build/espressif_esp32s3_default/espressif_esp32s3_default.bin
```

日常只更新应用（不要用 `--flash-mode dio`，否则会把 app 头改回 DIO）：

```bash
esptool --chip esp32s3 -p <PORT> -b 921600 write-flash \
  --flash-freq 80m --flash-size 4MB \
  0x10000 build/espressif_esp32s3_default/espressif_esp32s3_default.bin
```

```bash
esptool --chip esp32s3 image-info \
  build/espressif_esp32s3_default/espressif_esp32s3_default.bin
```

### 1.6 关于 `.px4` 与 QGC 固件升级

| 方式 | 是否支持 | 说明 |
|------|----------|------|
| `esptool write_flash` | **支持** | 正确烧写方式 |
| `make upload` / `px_uploader.py` | **不支持** | 面向 STM32 等 PX4 Bootloader 协议 |
| QGC「Firmware Upgrade」烧 `.px4` | **不支持** | 板端无 PX4 Bootloader |

`.px4` 为 JSON + 压缩 `.bin`（`board_id=888`，`magic=PX4ESP32v1`，`image_maxsize=2MiB`）。  
QGC 可通过 USB（`/dev/ttyACM0`，115200）作 **MAVLink 地面站**。

### 1.7 构建命令参考

```bash
export TOOLCHAIN_PATH=~/toolchain/xtensa-esp-elf-gcc/bin/
make espressif_esp32s3_default
```

产物：`build/espressif_esp32s3_default/espressif_esp32s3_default.{bin,px4,elf}`

### 1.8 启动路径（Stage 12 — 标准 rcS）

自 **2026-08 Stage 12** 起，NuttX 入口为 PX4 标准 **`rcS`**（不再使用 `rc.board_minboot` 作 init）：

| 项目 | 配置 |
|------|------|
| NuttX init | `CONFIG_NSH_INITSCRIPT="init.d/rcS"`（`nuttx-config/nsh/defconfig`） |
| 板级 hook | `rc.board_defaults`、`rc.board_sensors`、`rc.board_extras` |
| USB | `/dev/ttyACM0` **仅 NSH**；未编译 `cdcacm_autostart`，rcS 不会在 USB 上 autostart MAVLink |
| 串口 | PX4 标准 `rc.serial`（`GPS_1_CONFIG` / `MAV_0_CONFIG` / `RC_PORT_CONFIG`） |
| 默认机型 | **Quad X**（`SYS_AUTOSTART=4001`） |
| 固定翼 | QGC 改 `SYS_AUTOSTART` 为 FW airframe（如 2100）→ reboot |
| 地面车 | `50000` 差速 / `51000` Ackermann / `52000` Mecanum |
| minboot 历史 | 见 `/home/gina/test/test_PX4/esp32s3-bringup-archive/` |

验证：

```bash
boards/espressif/esp32s3/tools/run_test.sh   # 编译 + 烧写 + 50× help/ps/cat 压力测试
# 历史 minboot / stage restore：见 ../esp32s3-bringup-archive/
```

---

## 2. 硬件接口支持情况

### 2.1 总览

| 接口 / 功能 | 支持状态 | NuttX 设备 / 说明 |
|-------------|----------|-------------------|
| USB Serial（CDC ACM） | **已支持** | `/dev/ttyACM0`，**仅 NSH 控制台**（勿启 MAVLink） |
| UART0 / GPS1 | **已支持** | `/dev/ttyS0`，TX=**43**，RX=**44**；**GPS 硬件口**（QGC 配 `GPS_1_CONFIG=201`） |
| UART1 / TELEM1 | **已支持** | `/dev/ttyS1`，TX=**17**，RX=**18**；默认 **CRSF RC** |
| UART2 / TELEM2 | **已支持** | `/dev/ttyS2`，TX=**8**，RX=**3**；**MAVLink GCS** |
| I2C1 | **已支持** | SCL=15，SDA=16 |
| SPI2 | **已支持** | CLK=2，CS=1，MISO=41，MOSI=42 |
| PWM（LEDC，PX4 寄存器层） | **已支持** | GPIO 10 / 9 / 37 / 13 |
| SPI Flash / MTD 参数 | **已支持** | `/fs/mtd_params`，`0x310000` / 64KB |
| HRT | **已支持** | Timer Group 1 / Timer 0 |
| 板载 LED | **已支持** | GPIO12（蓝） |
| CRSF RC | **已配置** | `RC_PORT_CONFIG=101`（TELEM1 / ttyS1） |
| WiFi / `wlan0` | **未支持** | NuttX 10.3 无可用 WiFi |
| SPI3 / SDMMC / 片内 ADC / USB OTG / CAN | **未支持** | 未接入 |

### 2.2 串口、USB 与 CRSF

| 端口 | 设备节点 | 引脚 | 默认用途 | QGC 参数 |
|------|----------|------|----------|----------|
| USB CDC | `/dev/ttyACM0` | 内置 USB | **NSH 控制台** | — |
| UART0 | `/dev/ttyS0` | TX **43**，RX **44** | **GPS 硬件口** | QGC: `GPS_1_CONFIG=201` |
| UART1 | `/dev/ttyS1` | TX **17**，RX **18** | **CRSF 接收机**（TELEM1） | `RC_CRSF_PRT_CFG=101` |
| UART2 | `/dev/ttyS2` | TX **8**，RX **3** | **MAVLink GCS**（TELEM2） | `MAV_0_CONFIG=102` |

GPS 接线（3.3 V，u-blox 等）：

| GPS 模块 | ESP32-S3 |
|----------|----------|
| TX | GPIO **44**（UART0 RX） |
| RX | GPIO **43**（UART0 TX） |
| GND | GND |

默认波特率 **57600**（`SER_GPS1_BAUD`）；接好硬件后在 QGC 设置 **GPS 1** 端口并 reboot。

GPS 驱动已编入固件，`/dev/ttyS0` 节点可用；**boot 不自动 `gps start`**，`GPS_1_CONFIG` 默认 **0**，接模块后在 QGC 配置即可。

CRSF 接线（3.3 V）：

| 接收机 | ESP32-S3 |
|--------|----------|
| TX | GPIO **18**（UART1 RX） |
| RX（遥测可选） | GPIO **17**（UART1 TX） |
| GND | GND |

驱动在打开串口后自行设波特率 **420000**（`RC_CRSF_PRT_CFG=101`）。不要把 GPS 和 CRSF 配到同一串口。

MAVLink GCS 默认走 **TELEM2**（`/dev/ttyS2`，`MAV_0_CONFIG=102`），**不要**占用 `/dev/ttyACM0`。

### 2.3 I2C1

| 信号 | GPIO |
|------|------|
| SCL | **15** |
| SDA | **16** |

### 2.4 SPI2（BMI088 IMU）

| 信号 | GPIO | 说明 |
|------|------|------|
| CLK | **2** | SPI 时钟 |
| CS_ACC | **1** | BMI088 加速度计片选（GPIO，`board_config.h`） |
| CS_GYR | **38** | BMI088 陀螺仪片选（GPIO，`board_config.h`） |
| MISO | **41** | |
| MOSI | **42** | |
| DRDY | **40** | 数据就绪（可选，Acc/Gyr 共用） |

NuttX：`CONFIG_ESP32S3_SPI_SWCS` + `CONFIG_ESP32S3_SPI_UDCS` 启用后，片选由 `spi.cpp` 设备表里的 **GPIO 号** 逐设备控制（`SPI::CS{n}` = GPIO `n`）。PX4 每总线最多 **6** 个设备（`SPI_BUS_MAX_DEVICES`）。

PX4 设备表（`boards/espressif/esp32s3/src/spi.cpp`）：

| 设备 | devtype | 总线 |
|------|---------|------|
| BMI088 陀螺仪 | `DRV_GYR_DEVTYPE_BMI088` | SPI2 |
| BMI088 加速度计 | `DRV_ACC_DEVTYPE_BMI088` | SPI2 |

### 2.5 PWM 输出

| PX4 通道 | 功能参数 | GPIO | 含义 |
|----------|----------|------|------|
| MAIN1 | `PWM_MAIN_FUNC1=101` | **10** | Motor 1 |
| MAIN2 | `PWM_MAIN_FUNC2=103` | **9** | Motor 3 |
| MAIN3 | `PWM_MAIN_FUNC3=102` | **37** | Motor 2 |
| MAIN4 | `PWM_MAIN_FUNC4=104` | **13** | Motor 4 |

脉宽默认仍为 DIS/MIN=0、MAX=2100（电调接入前建议改成 1000/900 一类安全值）。

### 2.6 其他 GPIO

| 功能 | GPIO | 状态 |
|------|------|------|
| 状态 LED（蓝） | 12 | 已用 |
| Heater | 46 | 仅定义，未编 heater 驱动 |

---

## 3. 传感器与外设

### 3.1 总线分配

| 传感器 | 接口 | 总线 | 启动命令 |
|--------|------|------|----------|
| **BMI088** 加速度计 | **SPI** | SPI2 | `bmi088 -A -R 0 -s start` |
| **BMI088** 陀螺仪 | **SPI** | SPI2 | `bmi088 -G -R 0 -s start` |
| **IST8310** 磁力计 | **I2C** | I2C1 | `ist8310 start -I -b 1 -a 14 -R 0` |
| **BMP280** 气压计 | **I2C** | I2C1 | `bmp280 start -I -b 1` |

- **IMU（BMI088）走 SPI2**，与 I2C 传感器独立。
- **磁力计 + 气压计共用 I2C1**（SCL=GPIO15，SDA=GPIO16）。
- 启动脚本：`init/rc.board_sensors`（由标准 `rcS` 在传感器阶段调用）。

`default.px4board`（Stage 12 / rcS）启用驱动：

- `CONFIG_DRIVERS_IMU_BOSCH_BMI088=y`（SPI，非 `bmi088_i2c`）
- `CONFIG_DRIVERS_MAGNETOMETER_ISENTEK_IST8310=y`
- `CONFIG_DRIVERS_BAROMETER_BMP280=y`

未使用的 ICM/SPL06 等驱动在 Stage 12 **已关闭**，以减小镜像。

**固定翼 / 地面车**：固件已链 `fw_*`、`rover_*` 模块。切换时在 QGC 修改 `SYS_AUTOSTART` 并 reboot：

| SYS_AUTOSTART | 类型 |
|---------------|------|
| 4001 | Quad X（默认） |
| 21xx 等 | 固定翼 |
| 50000 | 差速 rover |
| 51000 | Ackermann 车 |
| 52000 | Mecanum 车 |

GPS：`CONFIG_DRIVERS_GPS=y`，`GPS_1_CONFIG=201`（QGC）→ `rc.serial` 自动 `gps start` on `/dev/ttyS0`。默认 `GPS_1_CONFIG=0`（不启 GPS）。

MAVLink：`MAV_0_CONFIG=102`（QGC / 默认）→ `rc.serial` 自动 `mavlink start` on `/dev/ttyS2`（`SER_TEL2_BAUD`，默认 115200）。

RC：`RC_PORT_CONFIG=101`（QGC / 默认）→ `rc.serial` + `rc_input` on `/dev/ttyS1`（CRSF 自动识别）。

Boot 走标准 **`rcS`** → `rc.board_defaults` → `rc.autostart` → `rc.serial`；**USB 仍仅 NSH**，不启 ttyACM0 MAVLink。

### 3.2 相关默认参数

| 参数 | 默认值 | 含义 |
|------|--------|------|
| `SYS_HAS_MAG` / `SYS_HAS_BARO` | 1 | 假定有磁/气压 |
| `SYS_AUTOSTART` | 4001 | Quad X（FW: 21xx / 车: 50000–52000） |
| `EKF2_EN` / `ATT_EN` | 1 / 0 | 多旋翼默认 EKF2；`attitude_estimator_q` 已编入备用 |
| `EKF2_HGT_REF` | 2 | 气压高度 |
| `EKF2_OF_CTRL` | **0** | 无光流 |
| `EKF2_RNG_CTRL` | **0** | 无测距 |
| `RC_PORT_CONFIG` | **101** | CRSF → TELEM1 / ttyS1 |
| `SENS_EXT_I2C_PRB` | **0** | 不探测外置 I2C 磁力计驱动 |
| `NAV_RCL_ACT` | 6 | RC 丢失 Disarm（不是 RTL） |

```sh
i2cdetect -b 1
```

---

## 4. 常见问题

**Q：只烧 `.bin` 到 `0x0`？**  
A：不可以。应用必须在 `0x10000`。

**Q：用上游默认分区表？**  
A：不可以。必须用本板 2MB `partitions.csv` 生成的表。

**Q：参数会随 `erase_flash` 丢吗？**  
A：会。`0x310000` 会被擦掉。

**Q：QGC 不能升级固件？**  
A：正常。用 esptool 烧 `.bin`。

**Q：CRSF 无遥控？**  
A：确认接收机 TX → GPIO18，3.3 V，`RC_PORT_CONFIG=101`，波特由驱动设为 420000。

**Q：BMI088 报 `no device on bus`？**  
A：查 **SPI2** 接线（CLK=2, CS_ACC=1, CS_GYR=38, MISO=41, MOSI=42）及 3.3 V；NSH 执行 `bmi088 -A -R 0 -s start` / `bmi088 -G -R 0 -s start` 单独测试。若 PCB 上 Gyr CS 不是 GPIO38，改 `board_config.h` 里 `BOARD_SPI2_CS_BMI088_GYRO`。

**Q：IST8310 / BMP280 找不到？**  
A：查 **I2C1**（SCL=15, SDA=16）；BMP280 地址通常 0x76 或 0x77，IST8310 为 **0x0E**（`-a 14`）。

---

## 5. 相关文件索引

| 路径 | 内容 |
|------|------|
| `boot/partitions.csv` | 2MB factory + 64KB params |
| `nuttx-config/nsh/defconfig` | `NSH_INITSCRIPT=init.d/rcS` |
| `init/rc.board_defaults` | 板级默认参数（MAV_0_CONFIG=102 → ttyS2 等） |
| `init/rc.board_sensors` | 传感器 + board_adc |
| `init/rc.board_extras` | 板级扩展（MAVLink 走 rc.serial） |
| `src/board_config.h` | PWM / LED / HRT / 栈 |
| `src/init.c` / `src/mtd.cpp` | 参数分区（须保持一致） |
| `firmware.prototype` | `board_id=888`，`image_maxsize=2MiB` |
