# PCIe MPS (Max Payload Size) Mismatch Detection - PR Documentation

## 概述

本 PR 为 pciutils 添加了 PCIe MPS（Max Payload Size）链路两端数值不同步的检测和警告功能。

## 问题背景

PCIe 链路两端的 MPS 设置如果不匹配，会导致：
1. **性能下降** - 实际传输的 payload 大小受限于较小的一端
2. **TLP 错误** - 严重不匹配可能导致传输错误
3. **兼容性问题** - 某些设备可能无法正常工作

## 实现功能

### 1. 新增命令行选项 `-A`

```bash
lspci -A    # 启用 MPS 不匹配检测
```

### 2. 检测的 MPS 问题

- **DevCtl MPS 不匹配**：链路两端当前 MPS 设置不同
- **MPS 未优化**：当前 MPS 不是两端能力的最小值
- **MPS 超出能力**：当前 MPS 超过了设备支持的最大值（配置错误）

### 3. 增强的 verbose 输出

在 `-v` 模式下，如果设备的 MPS 被限制（小于其能力值），会显示警告：

```
DevCtl: ... MaxPayload 128 bytes, MaxReadReq 512 bytes
        *** Warning: MPS limited to 128 bytes (device capable of 512 bytes)
```

## 示例输出

### MPS 不匹配警告

```
=== PCIe MPS (Max Payload Size) Mismatch Detection ===

*** MPS ERROR ***
  Device 1: 0000:00:01.0 (Root Port)
            DevCap MPS: 512 bytes, DevCtl MPS: 256 bytes
  Device 2: 0000:01:00.0 (Endpoint)
            DevCap MPS: 256 bytes, DevCtl MPS: 128 bytes
  Issue: Link partners have different MPS settings (256 vs 128 bytes). 
         This can cause TLP errors or performance degradation.
***

*** MPS WARNING ***
  Device 1: 0000:00:01.0 (Root Port)
            DevCap MPS: 512 bytes, DevCtl MPS: 256 bytes
  Device 2: 0000:01:00.0 (Endpoint)
            DevCap MPS: 256 bytes, DevCtl MPS: 128 bytes
  Issue: Device 2 MPS (128 bytes) is not set to minimum capability (256 bytes). 
         Optimal setting should be 256 bytes.
***

Checked 4 PCIe link(s) for MPS mismatches.
=== End of MPS Detection ===
```

## 代码变更

### 新增文件
- `ls-mps.c` - MPS 检测核心逻辑

### 修改文件
- `lspci.h` - 添加 MPS 函数声明
- `lspci.c` - 添加 `-A` 选项和调用
- `ls-caps.c` - 在 DevCtl 输出中添加 MPS 警告
- `Makefile` - 添加新源文件

## 技术细节

### MPS 检测算法

1. **扫描所有 PCIe 设备**，提取每个设备的：
   - DevCap MaxPayload（设备支持的最大值）
   - DevCtl MaxPayload（当前使用的值）
   - 设备类型（Root Port、Endpoint 等）

2. **识别链路对**：
   - 对于 Root Port/Downstream Port，查找其 secondary bus 上的设备
   - 对于 Endpoint/Upstream Port，查找其上游的 bridge

3. **执行检查**：
   - 比较链路两端的 DevCtl MPS
   - 检查是否设置为两端能力的最小值
   - 检查是否超出设备能力

### 数据结构

```c
struct mps_info {
    struct device *dev;
    int devcap_mps;      /* Maximum supported MPS */
    int devctl_mps;      /* Current MPS */
    int pcie_cap_offset; /* PCIe capability offset */
    int dev_type;        /* PCIe device type */
    unsigned int domain, bus, dev_num, func;
};
```

## 测试建议

1. **在 Linux 系统上测试**（macOS 需要特殊权限才能访问 PCI）：
   ```bash
   sudo ./lspci -A
   sudo ./lspci -vv | grep -i mps
   ```

2. **验证检测准确性**：
   - 检查 Root Port 和 Endpoint 的 MPS 是否匹配
   - 验证警告信息是否正确

3. **边界情况**：
   - 无 PCIe 设备的系统
   - 只有单个 Root Port 的系统
   - 复杂的 PCIe 拓扑结构

## 兼容性

- 遵循 pciutils 现有代码风格
- 使用标准 PCI/PCIe 寄存器定义
- 不影响现有功能（仅在 `-A` 选项时启用）

## 未来改进

1. 添加机器可读输出格式（`-mA`）
2. 支持设置 MPS 值（类似 setpci）
3. 添加更多 PCIe 链路健康检查
