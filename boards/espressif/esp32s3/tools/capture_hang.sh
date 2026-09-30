#!/usr/bin/env bash
# 长时间等待下一次卡死，在 panic 打印覆盖异常帧之前停住并写日志。
#
# 用法（板子已经启动、串口已经关掉之后）：
#   boards/espressif/esp32s3/tools/capture_hang.sh --wait
#
# 用 OpenOCD 在 xtensa_user_panic / xtensa_panic 下硬件断点，然后让两颗核继续跑。
# 不经过 GDB continue：GDB 恢复双核会再次把核停住，小灯不再闪。
# 看到「已继续运行」之后不要复位、不要打开串口。硬件断点在复位后会丢。
# 命中后日志在 /tmp/esp32s3_hang.log，其中 EXCVADDR 是非法访问的地址。
#
# 已经卡死、只抓当前现场（帧可能已被打印盖掉）：
#   boards/espressif/esp32s3/tools/capture_hang.sh --now
#
# 固件须与 ELF 同一次编译：
#   export PATH=~/toolchain/xtensa-esp-elf-gcc/bin:$PATH
#   make espressif_esp32s3_default

set -euo pipefail

MODE="${1:-}"
if [[ "${MODE}" != "--wait" && "${MODE}" != "--now" ]]; then
	echo "用法: $0 --wait | --now" >&2
	echo "  --wait  板子已启动后运行，一直等到下一次异常（几十分钟）。" >&2
	echo "  --now   已经卡死后抓当前现场。" >&2
	exit 2
fi

ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
# shellcheck disable=SC1091
source "${ROOT}/boards/espressif/esp32s3/tools/openocd_env.sh"

ELF="${ELF:-${ROOT}/build/espressif_esp32s3_default/espressif_esp32s3_default.elf}"
LOG="${LOG:-/tmp/esp32s3_hang.log}"
OCD_LOG="${OCD_LOG:-/tmp/esp32s3_openocd.log}"
export ELF LOG MODE
export PATH="${HOME}/toolchain/xtensa-esp-elf-gcc/bin:${PATH}"

if [[ ! -f "${ELF}" ]]; then
	echo "找不到 ${ELF}" >&2
	echo "先编译：export PATH=~/toolchain/xtensa-esp-elf-gcc/bin:\$PATH && make espressif_esp32s3_default" >&2
	exit 1
fi

if [[ ! -x "${OPENOCD}" ]]; then
	echo "OpenOCD 不存在。检查 openocd_env.sh 里的 OPENOCD。" >&2
	exit 1
fi

if command -v fuser >/dev/null 2>&1 && fuser /dev/ttyACM0 >/dev/null 2>&1; then
	echo "串口正被占用。先退出 picocom/minicom，OpenOCD 和 NSH 共用同一条 USB。" >&2
	fuser -v /dev/ttyACM0 >&2 || true
	exit 1
fi

NM="${HOME}/toolchain/xtensa-esp-elf-gcc/bin/xtensa-esp32s3-elf-nm"
if [[ ! -x "${NM}" ]]; then
	echo "找不到 ${NM}" >&2
	exit 1
fi

sym() {
	local addr
	addr="$("${NM}" -n "${ELF}" | awk -v name="$1" '$3 == name { print $1; exit }')"
	if [[ -z "${addr}" ]]; then
		echo "ELF 里没有符号 $1" >&2
		exit 1
	fi
	echo "0x${addr}"
}

export PANIC_USER="$(sym xtensa_user_panic)"
export PANIC="$(sym xtensa_panic)"
export READY="$(sym g_readytorun)"
export CURRENT_REGS="$(sym g_current_regs)"

if [[ "${MODE}" == "--wait" ]]; then
	cat <<'EOF'
等待下一次异常。不会复位。
1. 板子应已经启动。脚本会短暂停住，设上硬件断点，再让两颗核继续跑。
2. 看到「已继续运行」后小灯应重新闪。不要复位，不要打开串口。
3. 之后小灯若再次停住，就是这次卡死，脚本会自己写日志。
EOF
else
	echo "抓取当前现场。不会复位。异常帧可能已被 panic 打印覆盖。"
fi
echo "ELF: ${ELF}"
echo "断点: xtensa_user_panic ${PANIC_USER}  xtensa_panic ${PANIC}"
echo "日志: ${LOG}"

cleanup() {
	if [[ -n "${OCD_PID:-}" ]] && kill -0 "${OCD_PID}" 2>/dev/null; then
		if ! grep -q '^HIT$' "${LOG}" 2>/dev/null; then
			python3 - <<'PY' >/dev/null 2>&1 || true
import socket
try:
    s = socket.create_connection(("127.0.0.1", 4444), 1)
    s.sendall(b"rbp all\nesp32s3.cpu0 resume\nesp32s3.cpu1 resume\n")
    s.close()
except Exception:
    pass
PY
		fi
		kill "${OCD_PID}" 2>/dev/null || true
		wait "${OCD_PID}" 2>/dev/null || true
	fi
}
trap cleanup EXIT

: > "${OCD_LOG}"
# NuttX 不是 FreeRTOS。默认 RTOS 探测会在恢复双核时把连接拆掉。
"${OPENOCD}" -c "set ESP_RTOS none" -f board/esp32s3-builtin.cfg >"${OCD_LOG}" 2>&1 &
OCD_PID=$!

ready=0
for _ in $(seq 1 40); do
	if grep -q "Listening on port 4444" "${OCD_LOG}"; then
		ready=1
		break
	fi
	if ! kill -0 "${OCD_PID}" 2>/dev/null; then
		echo "OpenOCD 已退出。日志：${OCD_LOG}" >&2
		tail -40 "${OCD_LOG}" >&2 || true
		exit 1
	fi
	sleep 0.25
done

if [[ "${ready}" -ne 1 ]]; then
	echo "OpenOCD 没有打开 4444。日志：${OCD_LOG}" >&2
	tail -40 "${OCD_LOG}" >&2 || true
	exit 1
fi

python3 - <<'PY'
import glob
import os
import re
import socket
import subprocess
import time

MODE = os.environ["MODE"]
LOG_PATH = os.environ["LOG"]
ELF = os.environ["ELF"]
BP = [int(os.environ["PANIC_USER"], 16), int(os.environ["PANIC"], 16)]
READY = int(os.environ["READY"], 16)
CURRENT_REGS = int(os.environ["CURRENT_REGS"], 16)
NAME_OFF = 124
PID_OFF = 12

CAUSE = {
    0: "IllegalInstruction",
    2: "InstructionFetchError",
    3: "LoadStoreError",
    6: "IntegerDivideByZero",
    9: "LoadStoreAlignment",
    20: "InstFetchProhibited",
    28: "LoadProhibited",
    29: "StoreProhibited",
}
FRAME = {0: "EPC", 1: "PS", 2: "A0", 3: "A1", 19: "EXCCAUSE", 20: "EXCVADDR"}

logf = open(LOG_PATH, "w")

def emit(line):
    print(line, flush=True)
    logf.write(line + "\n")
    logf.flush()

class Ocd:
    def __init__(self):
        self.s = socket.create_connection(("127.0.0.1", 4444), 5)
        self.s.settimeout(0.4)
        self._drain()

    def _drain(self):
        try:
            while True:
                if not self.s.recv(4096):
                    break
        except socket.timeout:
            pass

    def cmd(self, text, wait=0.2):
        self.s.sendall((text + "\n").encode())
        time.sleep(wait)
        self.s.settimeout(0.3)
        data = b""
        try:
            while True:
                chunk = self.s.recv(4096)
                if not chunk:
                    break
                data += chunk
        except socket.timeout:
            pass
        return data.decode(errors="replace")

def states(text):
    found = {}
    for line in text.splitlines():
        for cpu in ("cpu0", "cpu1"):
            if "esp32s3." + cpu in line:
                if "running" in line:
                    found[cpu] = "running"
                elif "halted" in line:
                    found[cpu] = "halted"
    return found

def reg_value(text, name):
    match = re.search(r"\b%s \(/32\): (0x[0-9a-fA-F]+)" % name, text)
    if not match:
        return None
    return int(match.group(1), 16)

def words_of(text):
    vals = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        for item in line.split(":", 1)[1].split():
            if re.fullmatch(r"[0-9a-fA-F]{8}", item):
                vals.append(int(item, 16))
    return vals

def bytes_of(text):
    vals = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        for item in line.split(":", 1)[1].split():
            if re.fullmatch(r"[0-9a-fA-F]{2}", item):
                vals.append(int(item, 16))
    return vals

def read_regs(names):
    text = []
    values = {}
    for name in names:
        reply = ocd.cmd("reg %s" % name, wait=0.15)
        text.append(reply.strip())
        values[name] = reg_value(reply, name)
    return "\n".join(text), values

def symbolize(addr):
    if addr is None:
        return
    addr2line = os.path.expanduser(
        "~/toolchain/xtensa-esp-elf-gcc/bin/xtensa-esp32s3-elf-addr2line")
    if not os.path.isfile(addr2line):
        addr2line = "xtensa-esp32s3-elf-addr2line"
    bins = [ELF]
    roms = sorted(glob.glob(os.path.expanduser(
        "~/.espressif/tools/esp-rom-elfs/*/esp32s3_rev0_rom.elf")))
    if roms and 0x40000000 <= addr < 0x40070000:
        bins.append(roms[-1])
    for path in bins:
        try:
            out = subprocess.check_output(
                [addr2line, "-e", path, "-f", "-C", "-p", "0x%x" % addr],
                text=True, stderr=subprocess.DEVNULL).strip()
        except Exception as exc:
            out = str(exc)
        emit("  %s -> %s" % (os.path.basename(path), out))

def ptr_ok(value):
    return value is not None and 0x3fc00000 <= value < 0x40000000

ocd = Ocd()
ocd.cmd("reset_config none")
halted = ocd.cmd("halt", wait=1.0)
emit(halted.strip())

if MODE == "--wait":
    for addr in BP:
        reply = ocd.cmd("bp 0x%x 1 hw" % addr, wait=0.4)
        emit(reply.strip())
        if "breakpoint set at 0x%x" % addr not in reply:
            emit("硬件断点没有设上: 0x%x" % addr)
            raise SystemExit(1)
    ocd.cmd("resume", wait=0.5)
    running = {}
    for _ in range(20):
        running = states(ocd.cmd("targets", wait=0.3))
        if running.get("cpu0") == "running" and running.get("cpu1") == "running":
            break
        time.sleep(0.25)
    emit("targets cpu0=%s cpu1=%s" % (running.get("cpu0"), running.get("cpu1")))
    if running.get("cpu0") != "running" or running.get("cpu1") != "running":
        emit("两颗核没有继续跑，小灯会停。不要在这种状态下干等。")
        raise SystemExit(1)
    print("已继续运行，小灯应重新闪。从现在起不要复位，不要打开串口。", flush=True)
    started = time.time()
    next_beat = started + 60
    while True:
        if not os.path.exists("/proc/%d" % os.getppid()):
            raise SystemExit(1)
        try:
            now = states(ocd.cmd("targets", wait=0.3))
        except OSError as exc:
            emit("OpenOCD 连接断了: %s" % exc)
            raise SystemExit(1)
        if now.get("cpu0") == "halted" or now.get("cpu1") == "halted":
            emit("targets cpu0=%s cpu1=%s" % (now.get("cpu0"), now.get("cpu1")))
            break
        if time.time() >= next_beat:
            mins = int((time.time() - started) / 60)
            print("仍在等待，已 %d 分钟。不要复位，不要打开串口。" % mins, flush=True)
            next_beat += 60
        time.sleep(5)

emit("===== stop =====")
hit_cpu = None
REG_NAMES = ["pc", "a0", "a1", "a2", "a3", "exccause", "excvaddr", "epc1", "debugcause"]
snapshots = {}
for cpu in ("cpu0", "cpu1"):
    ocd.cmd("targets esp32s3.%s" % cpu, wait=0.2)
    text, values = read_regs(REG_NAMES)
    snapshots[cpu] = values
    emit("----- %s -----" % cpu)
    emit(text)
    pc = values.get("pc")
    debugcause = values.get("debugcause")
    if pc in BP or (debugcause is not None and debugcause & 0x2):
        hit_cpu = cpu

if hit_cpu is None:
    hit_cpu = "cpu0"
emit("dump %s" % hit_cpu)
values = snapshots[hit_cpu]
pc = values.get("pc")
a0 = values.get("a0")
a2 = values.get("a2")
a3 = values.get("a3")
spec_cause = values.get("exccause")
spec_vaddr = values.get("excvaddr")
epc1 = values.get("epc1")

ready_words = words_of(ocd.cmd("mdw 0x%x 1" % READY, wait=0.3))
if ready_words and ptr_ok(ready_words[0]):
    tcb = ready_words[0]
    pid_words = words_of(ocd.cmd("mdw 0x%x 1" % (tcb + PID_OFF), wait=0.3))
    name_bytes = bytes_of(ocd.cmd("mdb 0x%x 24" % (tcb + NAME_OFF), wait=0.3))
    name = bytes(b for b in name_bytes if b < 128)
    name = name.split(b"\x00", 1)[0].decode("ascii", errors="replace")
    pid = pid_words[0] if pid_words else None
    emit("task %s pid %s" % (name, pid))

frame_ptr = a3 if ptr_ok(a3) else None
if frame_ptr is None:
    cur = words_of(ocd.cmd("mdw 0x%x 2" % CURRENT_REGS, wait=0.3))
    for value in cur:
        if ptr_ok(value):
            frame_ptr = value
            break

emit("a2_exccause %s" % (("0x%x" % a2) if a2 is not None else None))
emit("spec_exccause %s %s" % (spec_cause, CAUSE.get(spec_cause, "")))
emit("spec_excvaddr %s" % (("0x%08x" % spec_vaddr) if spec_vaddr is not None else None))
emit("epc1 0x%08x" % (epc1 or 0))
symbolize(epc1)
emit("PC 0x%08x" % (pc or 0))
symbolize(pc)
if a0 is not None:
    ret = (a0 & 0x3fffffff) | 0x40000000
    emit("A0 0x%08x return 0x%08x" % (a0, ret))
    symbolize(ret)

if frame_ptr is None:
    emit("没有可用的异常帧指针")
else:
    raw = words_of(ocd.cmd("mdw 0x%x 24" % frame_ptr, wait=0.5))
    emit("FRAME 0x%x" % frame_ptr)
    for i, word in enumerate(raw):
        label = FRAME.get(i, "")
        extra = (" " + label) if label else ""
        emit("  [%2d]%s = 0x%08x" % (i, extra, word))
    if len(raw) > 20:
        epc = raw[0]
        fa0 = raw[2]
        cause = raw[19]
        vaddr = raw[20]
        ret = (fa0 & 0x3fffffff) | 0x40000000
        emit("EPC 0x%08x" % epc)
        symbolize(epc)
        emit("A0 0x%08x return 0x%08x" % (fa0, ret))
        symbolize(ret)
        emit("EXCCAUSE %d %s" % (cause, CAUSE.get(cause, "")))
        emit("EXCVADDR 0x%08x" % vaddr)

emit("HIT")
PY

if ! grep -q '^HIT$' "${LOG}"; then
	echo "没有捕到异常。" >&2
	echo "----- log -----" >&2
	tail -40 "${LOG}" >&2 || true
	echo "----- openocd -----" >&2
	tail -30 "${OCD_LOG}" >&2 || true
	exit 1
fi

echo "现场在 ${LOG}"
grep -E '^(EPC|A0|EXCCAUSE|EXCVADDR|task|spec_exccause|spec_excvaddr|PC) ' "${LOG}" || true
