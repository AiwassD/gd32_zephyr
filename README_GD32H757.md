# GD32H757 Zephyr 移植说明

本文档说明 `AiwassD/gd32_zephyr` 的 `gd32h757-port` 分支如何使用、如何配置环境，以及如何基于当前移植继续开发 GD32H757 应用。

当前分支基于官方 `GD32-MCU-IOT/gd32_zephyr`，目标是让 GD32H757 像已有 GD32H759 一样，拥有 Zephyr 可识别、可配置、可构建的芯片级入口，包括 SoC、DTS、pinctrl、board entry 和默认配置。

## 当前移植状态

已加入的 GD32H757 构建入口：

- `gd32h757zmt6_dev`：当前 GD32H757ZMT6 小系统板使用的入口。
- `gd32h757vit6_prod`：后续 GD32H757VIT6/LQFP100 硬件使用的入口。

已加入的主要芯片描述：

- `dts/arm/gd/gd32h7xx/gd32h757xx.dtsi`
- `dts/arm/gd/gd32h7xx/gd32h757zm.dtsi`
- `dts/arm/gd/gd32h7xx/gd32h757vi.dtsi`
- `soc/gd/gd32/gd32h7xx/Kconfig.defconfig.gd32h757`
- `boards/gd/gd32h757zmt6_dev/`
- `boards/gd/gd32h757vit6_prod/`

注意：本分支目前完成的是 GD32H757 的 Zephyr 构建入口和基础芯片级迁移。`hello_world` 和基础串口输出已验证可以构建，ZMT6 开发板的 USART0/PA9/PA10 串口输出已按当前开发板资料配置。其他外设仍需要结合后续原理图和实际硬件逐项 bring-up。

## 仓库关系

本仓库是 Zephyr 主工程 fork：

```text
https://github.com/AiwassD/gd32_zephyr.git
branch: gd32h757-port
```

GD32 的 pinctrl 定义位于 HAL 模块仓库中，因此本分支的 `west.yml` 已将 `hal_gigadevice` 指向配套 fork：

```text
https://github.com/AiwassD/hal_gigadevice_zephyr.git
branch: gd32h757-pinctrl
path: modules/hal/gigadevice
```

所以，使用本分支时需要通过 `west update` 拉取配套的 `hal_gigadevice`，否则 GD32H757 的 pinctrl 宏会缺失，构建会失败。

## Windows 11 + Anaconda 环境

以下命令以 PowerShell 为例。请不要在 `base` 环境中直接安装 Zephyr 依赖，建议创建独立环境：

```powershell
D:\Anaconda\Scripts\conda.exe create -n zephyr-gd32 python=3.12 -y
D:\Anaconda\Scripts\conda.exe install -n zephyr-gd32 -c conda-forge cmake ninja gperf -y
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 python -m pip install --upgrade pip
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 python -m pip install west pyocd pyserial
```

当前分支实际构建环境已验证：

- Windows 11
- Anaconda 独立环境 `zephyr-gd32`
- Python 3.12
- west 1.5.x
- CMake
- Ninja
- gperf
- Zephyr SDK，包含 `arm-zephyr-eabi` 工具链

如果你的 `conda.exe` 不在 `D:\Anaconda\Scripts\conda.exe`，可以用下面命令查找：

```powershell
where conda
```

## 获取源码

推荐在一个新的空目录中克隆本 fork 分支：

```powershell
git clone -b gd32h757-port https://github.com/AiwassD/gd32_zephyr.git gd32_zephyr_h757
cd gd32_zephyr_h757
```

初始化 west workspace：

```powershell
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west init -l .
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west update hal_gigadevice cmsis cmsis_6
```

如果当前目录位于另一个已经初始化过的 west workspace 内，`west init -l .` 可能会提示 workspace 已存在。此时可以在当前仓库根目录手动创建 `.west/config`：

```ini
[manifest]
path = .
file = west.yml
```

然后继续执行：

```powershell
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west update hal_gigadevice cmsis cmsis_6
```

安装 Zephyr Python 依赖：

```powershell
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west packages pip --install --ignore-venv-check
```

## Zephyr SDK

需要安装 Zephyr SDK，并确保包含 `arm-zephyr-eabi` 工具链。已安装 SDK 时，在当前 PowerShell 会话中设置：

```powershell
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = "D:\HS2\759To757\zephyr-sdk"
$env:PATH = "D:\HS2\759To757\zephyr-sdk\gnu\arm-zephyr-eabi\bin;" + $env:PATH
```

如果尚未安装 SDK，可以使用 west 安装到指定目录：

```powershell
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west sdk install -d D:\HS2\759To757\zephyr-sdk -t arm-zephyr-eabi
```

`DTC` 未安装时，当前 `hello_world` 和基础串口程序可能只出现警告并继续构建。但后续使用更复杂的 devicetree 功能时建议安装 DTC。

## 构建 hello_world

进入仓库根目录后，设置当前 workspace：

```powershell
$env:WEST_TOPDIR = (Get-Location).Path
$env:ZEPHYR_BASE = (Get-Location).Path
```

构建 GD32H757ZMT6：

```powershell
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west build -p always -b gd32h757zmt6_dev samples\hello_world
```

构建 GD32H757VIT6：

```powershell
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west build -p always -b gd32h757vit6_prod samples\hello_world
```

构建产物位于：

```text
build/zephyr/zephyr.hex
build/zephyr/zephyr.elf
build/zephyr/zephyr.bin
```

## 新建一个最小应用

可以在仓库内创建自己的应用，例如：

```text
my_apps/h757_hello/
├── CMakeLists.txt
├── prj.conf
└── src/
    └── main.c
```

`CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(h757_hello)

target_sources(app PRIVATE src/main.c)
```

`prj.conf`：

```conf
CONFIG_PRINTK=y
```

`src/main.c`：

```c
#include <zephyr/kernel.h>

int main(void)
{
	printk("Hello GD32H757 Zephyr\n");
	return 0;
}
```

构建：

```powershell
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west build -p always -b gd32h757zmt6_dev my_apps\h757_hello
```

## ZMT6 开发板串口说明

当前 ZMT6 小系统板资料中，板载 USB-C 转串口芯片为 CH340，连接到 MCU 的 USART0：

```text
PA9  -> USART0_TX -> CH340 RXD
PA10 -> USART0_RX -> CH340 TXD
```

当前 `gd32h757zmt6_dev` 的默认控制台已按 USART0、PA9、PA10、115200 8N1 配置。串口运行参数：

```text
Baud rate: 115200
Data bits: 8
Parity: none
Stop bits: 1
Flow control: none
```

如果要写一个循环输出 `1..100` 的测试程序，可创建 `my_apps/com_counter`。

`my_apps/com_counter/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(com_counter)

target_sources(app PRIVATE src/main.c)
```

`my_apps/com_counter/prj.conf`：

```conf
CONFIG_SERIAL=y
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n
CONFIG_PRINTK=n
CONFIG_BOOT_BANNER=n
```

`my_apps/com_counter/src/main.c`：

```c
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static void uart_write(const struct device *uart, const char *data)
{
	while (*data != '\0') {
		uart_poll_out(uart, *data++);
	}
}

int main(void)
{
	const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	char line[8];

	if (!device_is_ready(uart)) {
		return 0;
	}

	while (1) {
		for (unsigned int number = 1; number <= 100; number++) {
			snprintf(line, sizeof(line), "%u\r\n", number);
			uart_write(uart, line);
			k_msleep(100);
		}
	}
}
```

构建：

```powershell
D:\Anaconda\Scripts\conda.exe run -n zephyr-gd32 west build -p always -b gd32h757zmt6_dev my_apps\com_counter
```

## 烧录和运行

构建后可使用 `build/zephyr/zephyr.hex`。

如果使用 GD32 All In One Programmer 串口下载，可按开发板 BOOT/RESET 进入 BootLoader/ISP 模式，然后选择：

```text
Interface: COM
BootLoader: UART
Port: 对应 CH340 的 COM 口
Start Address: 0x08000000
File: build/zephyr/zephyr.hex
```

下载完成后复位运行应用。运行时串口与下载串口通常是同一个 CH340 COM 口，但运行阶段的波特率应使用应用配置的 `115200 8N1`。

## 后续开发建议

如果要继续扩展 H757 支持，建议按下面顺序推进：

1. 先保持 `samples/hello_world` 和最小串口程序可构建。
2. 每新增一个外设，先补 DTS 节点、pinctrl 和 board overlay，再写最小验证程序。
3. ZMT6 开发板和 VIT6 目标硬件应使用不同 board entry，避免把开发板临时连接误写进芯片级 dtsi。
4. 与上游对比时，使用 `gd32h757-port` 分支和官方 `main` 分支比较。
5. 若修改 pinctrl 宏，需要同步修改并推送 `AiwassD/hal_gigadevice_zephyr` 的 `gd32h757-pinctrl` 分支。

