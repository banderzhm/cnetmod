#!/usr/bin/env python3
"""
Network Emulation Wrapper
=========================
封装网络条件配置，支持跨平台网络模拟。

Linux: 使用 tc netem（需要 root 权限）
Windows: 使用 Clumsy 或 dummynet（需要安装）

Usage:
    from network_emulation import create_emulator, PRESETS

    emulator = create_emulator()
    with emulator.emulate(PRESETS["wifi_bad"]):
        # Run tests under bad WiFi conditions
        ...
"""

import os
import sys
import json
import subprocess
import time
from dataclasses import dataclass, field, asdict
from typing import Optional, List, Dict
from contextlib import contextmanager
import platform
import logging

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Network Condition Data Class
# ---------------------------------------------------------------------------

@dataclass
class NetworkCondition:
    """
    网络条件配置

    Attributes:
        delay_ms:         固定延迟 (毫秒)
        jitter_ms:        延迟抖动 (毫秒)
        delay_correlation: 延迟相关性 (0-100%)
        loss_rate:        丢包率 (0-100%)
        loss_correlation: 丢包相关性 (0-100%)
        reorder_rate:     乱序率 (0-100%)
        reorder_gap:      乱序间隔 (包数)
        bandwidth_kbps:   带宽限制 (kbps)，None 表示不限制
        duplicate_rate:   重复率 (0-100%)
        corrupt_rate:     损坏率 (0-100%)
    """
    # 延迟
    delay_ms: float = 0
    jitter_ms: float = 0
    delay_correlation: float = 0  # 延迟相关性 (0-100%)

    # 丢包
    loss_rate: float = 0  # 丢包率 (0-100%)
    loss_correlation: float = 0  # 丢包相关性

    # 乱序
    reorder_rate: float = 0  # 乱序率 (0-100%)
    reorder_gap: int = 0  # 乱序间隔

    # 带宽
    bandwidth_kbps: Optional[int] = None  # 带宽限制 (kbps)

    # 重复
    duplicate_rate: float = 0  # 重复率

    # 损坏
    corrupt_rate: float = 0  # 损坏率

    def replace(self, **kwargs) -> "NetworkCondition":
        """返回替换了指定字段的新 NetworkCondition 实例"""
        data = asdict(self)
        data.update(kwargs)
        return NetworkCondition(**data)

    def is_trivial(self) -> bool:
        """判断是否为理想网络（无任何限制）"""
        return (
            self.delay_ms == 0
            and self.jitter_ms == 0
            and self.loss_rate == 0
            and self.reorder_rate == 0
            and self.bandwidth_kbps is None
            and self.duplicate_rate == 0
            and self.corrupt_rate == 0
        )

    def describe(self) -> str:
        """返回人类可读的描述"""
        parts = []
        if self.delay_ms > 0:
            parts.append(f"delay={self.delay_ms}ms")
            if self.jitter_ms > 0:
                parts.append(f"jitter=±{self.jitter_ms}ms")
        if self.loss_rate > 0:
            parts.append(f"loss={self.loss_rate}%")
        if self.reorder_rate > 0:
            parts.append(f"reorder={self.reorder_rate}% (gap={self.reorder_gap})")
        if self.bandwidth_kbps:
            if self.bandwidth_kbps >= 1000:
                parts.append(f"bw={self.bandwidth_kbps / 1000:.0f}Mbps")
            else:
                parts.append(f"bw={self.bandwidth_kbps}kbps")
        if self.duplicate_rate > 0:
            parts.append(f"dup={self.duplicate_rate}%")
        if self.corrupt_rate > 0:
            parts.append(f"corrupt={self.corrupt_rate}%")
        return ", ".join(parts) if parts else "perfect"

    @classmethod
    def from_dict(cls, data: Dict) -> "NetworkCondition":
        """从字典创建 NetworkCondition"""
        valid_fields = {f.name for f in cls.__dataclass_fields__.values()}
        filtered = {k: v for k, v in data.items() if k in valid_fields}
        return cls(**filtered)


# ---------------------------------------------------------------------------
# Base Emulator
# ---------------------------------------------------------------------------

class NetworkEmulator:
    """网络模拟器抽象基类"""

    def __init__(self, interface: str = "lo", port: int = 4433):
        self.interface = interface
        self.port = port
        self.platform = platform.system()
        self._active = False
        self._current_condition: Optional[NetworkCondition] = None

    def apply(self, condition: NetworkCondition) -> bool:
        """应用网络条件"""
        raise NotImplementedError

    def clear(self) -> bool:
        """清除所有网络条件"""
        raise NotImplementedError

    @contextmanager
    def emulate(self, condition: NetworkCondition):
        """
        上下文管理器：进入时应用网络条件，退出时清除。

        Usage:
            with emulator.emulate(PRESETS["wifi_bad"]):
                run_tests()
        """
        try:
            success = self.apply(condition)
            if not success:
                logger.warning("Failed to apply network condition, continuing without emulation")
            yield
        finally:
            self.clear()

    def is_available(self) -> bool:
        """检查网络模拟工具是否可用"""
        raise NotImplementedError

    @property
    def current_condition(self) -> Optional[NetworkCondition]:
        """当前生效的网络条件"""
        return self._current_condition if self._active else None


# ---------------------------------------------------------------------------
# Linux: tc netem
# ---------------------------------------------------------------------------

class TcNetemEmulator(NetworkEmulator):
    """
    Linux tc netem 实现

    需要 root 权限或 CAP_NET_ADMIN capability。
    支持延迟、丢包、乱序、重复、损坏、带宽限制等全部网络条件。
    """

    def apply(self, condition: NetworkCondition) -> bool:
        """应用 tc netem 规则"""
        if not self.is_available():
            logger.error("tc command not found or insufficient permissions")
            return False

        # 先清除已有规则
        self._clear_silent()

        # 如果是理想网络，不需要添加规则
        if condition.is_trivial():
            self._active = True
            self._current_condition = condition
            return True

        # 构建 tc netem 命令
        cmd = ["tc", "qdisc", "add", "dev", self.interface, "root", "netem"]

        # 延迟
        if condition.delay_ms > 0:
            cmd.extend(["delay", f"{condition.delay_ms}ms"])
            if condition.jitter_ms > 0:
                cmd.append(f"{condition.jitter_ms}ms")
                if condition.delay_correlation > 0:
                    cmd.append(f"{condition.delay_correlation}%")

        # 丢包
        if condition.loss_rate > 0:
            cmd.extend(["loss", "random", f"{condition.loss_rate}%"])
            if condition.loss_correlation > 0:
                cmd.append(f"{condition.loss_correlation}%")

        # 乱序（需要在延迟之后）
        if condition.reorder_rate > 0 and condition.delay_ms > 0:
            cmd.extend(["reorder", f"{condition.reorder_rate}%"])
            if condition.reorder_gap > 0:
                cmd.append(f"{condition.reorder_gap}")

        # 重复
        if condition.duplicate_rate > 0:
            cmd.extend(["duplicate", f"{condition.duplicate_rate}%"])

        # 损坏
        if condition.corrupt_rate > 0:
            cmd.extend(["corrupt", f"{condition.corrupt_rate}%"])

        # 执行 netem 命令
        logger.info(f"Applying tc netem: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode != 0:
            logger.error(f"tc netem failed: {result.stderr.strip()}")
            return False

        # 带宽限制使用 tbf（Token Bucket Filter）
        if condition.bandwidth_kbps:
            cmd_tbf = [
                "tc", "qdisc", "add", "dev", self.interface,
                "parent", "1:1", "handle", "10:",
                "tbf", "rate", f"{condition.bandwidth_kbps}kbit",
                "burst", "32kbit", "latency", "400ms"
            ]
            logger.info(f"Applying tc tbf: {' '.join(cmd_tbf)}")
            result_tbf = subprocess.run(cmd_tbf, capture_output=True, text=True)

            if result_tbf.returncode != 0:
                logger.error(f"tc tbf failed: {result_tbf.stderr.strip()}")
                # 回滚 netem
                self._clear_silent()
                return False

        self._active = True
        self._current_condition = condition
        logger.info(f"Network condition applied: {condition.describe()}")
        return True

    def clear(self) -> bool:
        """清除 tc 规则"""
        return self._clear_silent()

    def _clear_silent(self) -> bool:
        """静默清除，不记录警告"""
        if not self._active:
            # 仍然尝试清除，以防状态不同步
            pass

        cmd = ["tc", "qdisc", "del", "dev", self.interface, "root"]
        result = subprocess.run(cmd, capture_output=True, text=True)

        self._active = False
        self._current_condition = None

        if result.returncode != 0 and "RTNETLINK answers: No such file" not in result.stderr:
            logger.debug(f"tc del note: {result.stderr.strip()}")

        return True

    def is_available(self) -> bool:
        """检查 tc 是否可用（需要 root 权限）"""
        try:
            result = subprocess.run(
                ["tc", "qdisc", "show"],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False

    def show_rules(self) -> str:
        """显示当前的 tc 规则"""
        try:
            result = subprocess.run(
                ["tc", "qdisc", "show", "dev", self.interface],
                capture_output=True,
                text=True,
                timeout=5
            )
            return result.stdout.strip()
        except Exception as e:
            return f"Error: {e}"


# ---------------------------------------------------------------------------
# Windows: Clumsy
# ---------------------------------------------------------------------------

class ClumsyEmulator(NetworkEmulator):
    """
    Windows Clumsy 实现

    需要手动安装 Clumsy: https://jagt.github.io/clumsy/
    Clumsy 通过 WinDivert 拦截网络包并应用延迟、丢包等效果。
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._clumsy_paths = [
            r"C:\Program Files\Clumsy\clumsy.exe",
            r"C:\Program Files (x86)\Clumsy\clumsy.exe",
            r"C:\Tools\Clumsy\clumsy.exe",
            os.path.expanduser(r"~\Clumsy\clumsy.exe"),
        ]
        self._process: Optional[subprocess.Popen] = None

    def _find_clumsy(self) -> Optional[str]:
        """搜索 Clumsy 可执行文件"""
        for path in self._clumsy_paths:
            if os.path.exists(path):
                return path
        # 也检查 PATH
        try:
            result = subprocess.run(
                ["where", "clumsy.exe"],
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                return result.stdout.strip().split("\n")[0]
        except Exception:
            pass
        return None

    def apply(self, condition: NetworkCondition) -> bool:
        """启动 Clumsy 进程"""
        clumsy_path = self._find_clumsy()
        if not clumsy_path:
            logger.error(
                "Clumsy not found. Download from: https://jagt.github.io/clumsy/"
            )
            return False

        # 构建 Clumsy 命令行参数
        args = [clumsy_path]

        # 过滤本地端口的流量
        filter_expr = f"tcp.DstPort == {self.port} or tcp.SrcPort == {self.port}"
        args.extend(["--filter", filter_expr])

        if condition.delay_ms > 0:
            args.extend(["--lag", f"{int(condition.delay_ms)}"])

        if condition.loss_rate > 0:
            args.extend(["--drop", f"{int(condition.loss_rate * 10)}"])  # Clumsy 用千分比

        if condition.reorder_rate > 0:
            args.extend(["--out-of-order", f"{int(condition.reorder_rate * 10)}"])

        if condition.duplicate_rate > 0:
            args.extend(["--dupe", f"{int(condition.duplicate_rate * 10)}"])

        if condition.corrupt_rate > 0:
            args.extend(["--tamper", f"{int(condition.corrupt_rate * 10)}"])

        # 启动 Clumsy 进程
        try:
            logger.info(f"Starting Clumsy: {' '.join(args)}")
            self._process = subprocess.Popen(
                args,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            # 等待 Clumsy 初始化
            time.sleep(0.5)

            if self._process.poll() is not None:
                logger.error("Clumsy process exited immediately")
                return False

            self._active = True
            self._current_condition = condition
            logger.info(f"Clumsy started (PID {self._process.pid}): {condition.describe()}")
            return True

        except Exception as e:
            logger.error(f"Failed to start Clumsy: {e}")
            return False

    def clear(self) -> bool:
        """停止 Clumsy 进程"""
        if self._process:
            try:
                self._process.terminate()
                self._process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait(timeout=2)
            except Exception as e:
                logger.warning(f"Error stopping Clumsy: {e}")
            finally:
                self._process = None

        self._active = False
        self._current_condition = None
        return True

    def is_available(self) -> bool:
        """检查 Clumsy 是否可用"""
        return self._find_clumsy() is not None


# ---------------------------------------------------------------------------
# Windows: dummynet / windivert alternative
# ---------------------------------------------------------------------------

class DummyNetEmulator(NetworkEmulator):
    """
    Windows dummynet (ipfw) 实现（备选方案）

    需要安装 dummynet: https://www.ics.uci.edu/~atm/papers/dummynet/
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._rules_applied: List[str] = []

    def apply(self, condition: NetworkCondition) -> bool:
        """应用 dummynet 规则"""
        if not self.is_available():
            logger.error("ipfw/dummynet not found")
            return False

        pipe_num = 1
        rules = []

        # 创建 pipe
        pipe_cmd = ["ipfw", "pipe", str(pipe_num), "config"]

        if condition.bandwidth_kbps:
            pipe_cmd.extend(["bw", f"{condition.bandwidth_kbps}Kbit/s"])

        if condition.delay_ms > 0:
            pipe_cmd.extend(["delay", f"{int(condition.delay_ms)}ms"])

        if condition.loss_rate > 0:
            pipe_cmd.extend(["plr", f"{condition.loss_rate / 100}"])

        # 执行 pipe 配置
        try:
            result = subprocess.run(pipe_cmd, capture_output=True, text=True, timeout=10)
            if result.returncode != 0:
                logger.error(f"ipfw pipe config failed: {result.stderr}")
                return False
            rules.append(" ".join(pipe_cmd))
        except Exception as e:
            logger.error(f"ipfw failed: {e}")
            return False

        # 添加转发规则
        fwd_cmd = [
            "ipfw", "add", "pipe", str(pipe_num),
            "tcp", "from", "any", "to", "any", str(self.port)
        ]
        try:
            result = subprocess.run(fwd_cmd, capture_output=True, text=True, timeout=10)
            if result.returncode != 0:
                logger.error(f"ipfw fwd rule failed: {result.stderr}")
                return False
            self._rules_applied.append(" ".join(fwd_cmd))
        except Exception as e:
            logger.error(f"ipfw fwd rule failed: {e}")
            return False

        self._active = True
        self._current_condition = condition
        return True

    def clear(self) -> bool:
        """清除 dummynet 规则"""
        try:
            subprocess.run(
                ["ipfw", "-q", "flush"],
                capture_output=True, text=True, timeout=10
            )
        except Exception:
            pass

        self._rules_applied.clear()
        self._active = False
        self._current_condition = None
        return True

    def is_available(self) -> bool:
        """检查 ipfw 是否可用"""
        try:
            result = subprocess.run(
                ["ipfw", "list"],
                capture_output=True, text=True, timeout=5
            )
            return result.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            return False


# ---------------------------------------------------------------------------
# Dummy (no-op) Emulator
# ---------------------------------------------------------------------------

class DummyNetemEmulator(NetworkEmulator):
    """
    空操作模拟器

    用于不支持网络模拟的平台，仅打印警告，不实际修改网络条件。
    测试仍可运行，但不会有真实的弱网效果。
    """

    def apply(self, condition: NetworkCondition) -> bool:
        if not condition.is_trivial():
            logger.warning(
                f"Network emulation not supported on {self.platform}. "
                f"Requested: {condition.describe()}. "
                f"Tests will run WITHOUT network emulation."
            )
        self._active = True
        self._current_condition = condition
        return True

    def clear(self) -> bool:
        self._active = False
        self._current_condition = None
        return True

    def is_available(self) -> bool:
        return True


# ---------------------------------------------------------------------------
# Factory
# ---------------------------------------------------------------------------

def create_emulator(interface: str = "lo", port: int = 4433) -> NetworkEmulator:
    """
    工厂函数：根据当前平台创建合适的网络模拟器。

    优先级：
    - Linux:   TcNetemEmulator > DummyNetemEmulator
    - Windows: ClumsyEmulator > DummyNetEmulator > DummyNetemEmulator
    - Other:   DummyNetemEmulator
    """
    system = platform.system()

    if system == "Linux":
        emulator = TcNetemEmulator(interface, port)
        if emulator.is_available():
            logger.info("Using tc netem for network emulation")
            return emulator
        logger.warning("tc netem not available, falling back to dummy emulator")

    elif system == "Windows":
        # 优先尝试 Clumsy
        clumsy = ClumsyEmulator(interface, port)
        if clumsy.is_available():
            logger.info("Using Clumsy for network emulation")
            return clumsy

        # 尝试 dummynet
        dummynet = DummyNetEmulator(interface, port)
        if dummynet.is_available():
            logger.info("Using dummynet for network emulation")
            return dummynet

        logger.warning("No Windows network emulation tool found, falling back to dummy emulator")

    elif system == "Darwin":
        # macOS 可使用 dnctl/pf 但暂不实现
        logger.warning("macOS network emulation not yet implemented, using dummy emulator")

    return DummyNetemEmulator(interface, port)


# ---------------------------------------------------------------------------
# Preset Network Conditions
# ---------------------------------------------------------------------------

PRESETS: Dict[str, NetworkCondition] = {
    # 理想网络
    "perfect": NetworkCondition(),

    # WiFi 场景
    "wifi_good": NetworkCondition(
        delay_ms=20,
        jitter_ms=5,
        loss_rate=0.1
    ),
    "wifi_bad": NetworkCondition(
        delay_ms=100,
        jitter_ms=50,
        loss_rate=2.0
    ),

    # 移动网络
    "mobile_4g": NetworkCondition(
        delay_ms=50,
        jitter_ms=20,
        loss_rate=0.5,
        bandwidth_kbps=10000
    ),
    "mobile_3g": NetworkCondition(
        delay_ms=200,
        jitter_ms=100,
        loss_rate=1.0,
        bandwidth_kbps=1000
    ),

    # 卫星连接
    "satellite": NetworkCondition(
        delay_ms=600,
        jitter_ms=100,
        loss_rate=2.0,
        bandwidth_kbps=500
    ),

    # 单一变量 - 丢包率
    "loss_0.1pct": NetworkCondition(loss_rate=0.1),
    "loss_1pct": NetworkCondition(loss_rate=1.0),
    "loss_5pct": NetworkCondition(loss_rate=5.0),
    "loss_10pct": NetworkCondition(loss_rate=10.0),

    # 单一变量 - 延迟
    "delay_50ms": NetworkCondition(delay_ms=50, jitter_ms=5),
    "delay_100ms": NetworkCondition(delay_ms=100, jitter_ms=10),
    "delay_200ms": NetworkCondition(delay_ms=200, jitter_ms=20),
    "delay_500ms": NetworkCondition(delay_ms=500, jitter_ms=50),

    # 单一变量 - 带宽
    "bw_1mbps": NetworkCondition(bandwidth_kbps=1000),
    "bw_10mbps": NetworkCondition(bandwidth_kbps=10000),
    "bw_100mbps": NetworkCondition(bandwidth_kbps=100000),

    # 单一变量 - 乱序
    "reorder_2pct": NetworkCondition(reorder_rate=2.0, reorder_gap=5),
    "reorder_5pct": NetworkCondition(reorder_rate=5.0, reorder_gap=3),
    "reorder_10pct": NetworkCondition(reorder_rate=10.0, reorder_gap=2),
}


def load_presets_from_json(json_path: str) -> Dict[str, NetworkCondition]:
    """
    从 JSON 文件加载预设网络条件。

    Args:
        json_path: JSON 文件路径

    Returns:
        预设字典，键为名称，值为 NetworkCondition
    """
    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    presets = {}
    for name, config in data.get("presets", {}).items():
        presets[name] = NetworkCondition.from_dict(config)

    return presets


# ---------------------------------------------------------------------------
# CLI (for standalone testing)
# ---------------------------------------------------------------------------

def _print_presets():
    """打印所有预设条件"""
    print(f"\n{'Name':<20} {'Description'}")
    print(f"{'-'*20} {'-'*60}")
    for name, cond in sorted(PRESETS.items()):
        print(f"{name:<20} {cond.describe()}")
    print()


def main():
    """独立运行时打印预设和平台信息"""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    print(f"Platform: {platform.system()}")
    print(f"Python: {sys.version}")

    emulator = create_emulator()
    print(f"Emulator: {type(emulator).__name__}")
    print(f"Available: {emulator.is_available()}")

    _print_presets()

    if len(sys.argv) > 1 and sys.argv[1] == "--test":
        print("Testing emulator apply/clear cycle...")
        test_condition = NetworkCondition(delay_ms=100, loss_rate=1.0)
        with emulator.emulate(test_condition):
            print(f"  Active: {emulator._active}")
            print(f"  Current: {emulator.current_condition}")
            if isinstance(emulator, TcNetemEmulator):
                print(f"  Rules: {emulator.show_rules()}")
        print(f"  After clear - Active: {emulator._active}")
        print("Done.")


if __name__ == "__main__":
    main()
