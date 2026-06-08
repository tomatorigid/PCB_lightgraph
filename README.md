# PCB_lightgraph

[![GitHub Downloads](https://img.shields.io/github/downloads/tomatorigid/PCB_lightgraph/total.svg)](https://github.com/tomatorigid/PCB_lightgraph/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Qt](https://img.shields.io/badge/Framework-Qt%205.x-blue.svg)](https://www.qt.io/)

---

## 项目定位 | Project Positioning

**PCB_lightgraph** 是一个将 2D 插画快速转换为可制造 PCB 分层图纸的桌面工具，目标是把“视觉设计”与“可生产工艺”之间的转换流程自动化。

**PCB_lightgraph** is a desktop utility that transforms 2D artwork into manufacturable PCB layer images, bridging visual design and PCB production constraints.

它主要面向两类用户：

1. 想制作透光 PCB 艺术画的硬件工程师/创客。
2. 希望把插画快速映射为工艺层（铜层/阻焊/丝印/透光层）的设计者。

---

## 核心能力总览 | Core Capability Overview

### 1) 多层图像自动拆分 | Multi-layer Decomposition
- Top Copper（线路/金属质感）
- Top Mask（阻焊开窗）
- Top Silk（丝印图形）
- Bottom Mask（背面透光）

### 2) 灯光布局与预览 | LED Layout and Rendering
- 自动布灯（重心建议）+ 手动布灯
- 叠加灯光范围预览
- 散射半径和中心不透明度可调

### 3) 边缘处理双模式 | Dual Edge Operation Modes
- 边缘增强（Edge Enhance）
- 描边（Canny Stroke）
- 两者共享阈值与预处理参数，便于统一调参

### 4) 工程保存/导入 | Project Save/Load
- 支持 `*.pcblg` 工程包

### 5) 实时外部编辑联动 | External Live Editing
- 菜单 `File -> 画图实时编辑`

---

## 重点技术与算法说明 | Key Technologies and Algorithms

## 1. 图像分层核心
基于阈值与颜色/亮度规则，将输入图像拆分为 PCB 制造所需层。通过统一流程输出四层图，支持后续导出。

## 2. 边缘增强与描边
- **边缘增强**：用于强化轮廓与细节边界。
- **Canny 描边**：用于更稳定的线条提取，适合插画轮廓呈现。
- **自动反色范围**：按局部环境决定边缘色，减轻明暗背景下描边对比不足问题。

## 3. 高斯预滤波
- 可开启/关闭。
- 在边缘处理前降噪，减少杂点和不需要细节。
- 用于提升边缘增强/描边结果稳定性。

## 4. Douglas-Peucker（实验性）
- 对路径进行抽稀简化，减少冗余点。
- 用于实验性轮廓简化场景，支持容忍度和线宽参数。

## 5. 自动布灯建议
- 基于图像分布与权重进行候选点建议。
- 支持手动覆盖，适配复杂图案的实际需求。

---

## 性能优化与工程化改进 | Performance and Engineering Improvements

### 1) 边缘计算缓存化（重大）
对边缘处理路径增加缓存策略，避免重复计算，边缘模式切换与反复预览时响应明显改善。

### 2) 大图导入稳定性改进
对超大分辨率输入进行像素上限预判和比例缩放导入，降低崩溃风险（作者环境实测稳定）。

### 3) 图片格式兼容增强
增加文件头格式检测并处理“后缀与真实编码不一致”的情况，修复此前部分错误编码图片无法打开的问题。

### 4) 临时工作区机制（temp）
- 运行目录维护 `temp` 工作区
- 图片导入复制到 temp
- 维护 `args.json` 参数快照
- 程序退出尝试清理 temp 图片

### 5) 外部编辑热更新
通过定时检测 temp 图片时间戳/大小变化，在外部编辑保存后自动重载主流程，形成“编辑-回看”闭环。

---

## 界面与交互流程 | UI and Workflow

### 主界面区块
- 左侧：综合预览（支持缩放、平移、手动布灯）
- 中间：参数控制（基础参数、灯光、裸露基材、边缘操作、导出）
- 右侧：四层分图预览

### 菜单入口
- `File -> 导入图片`
- `File -> 导出图纸`
- `File -> 保存工程...`（`*.pcblg`）
- `File -> 导入工程...`（`*.pcblg`）
- `File -> 画图实时编辑`

### 边缘操作逻辑
- 总开关 `边缘操作` 控制子项显示
- 模式二选一：`描边` / `边缘增强`
- 共用阈值与自动反色范围
- 预滤波、DP 设置统一影响边缘链路

---


## 使用指南 | Usage Guide

1. 打开程序并点击 `导入图片`
2. 调整基础参数（工艺、阻焊色、阈值）
3. 视需求开启边缘操作，选择描边或增强
4. 使用自动/手动布灯查看综合效果
5. 导出四层图纸用于后续 EDA 流程
6. 若需中途保存，使用 `保存工程 (.pcblg)`
7. 若需外部修图，使用 `画图实时编辑`，在画图中 `Ctrl+S` 后回到程序自动重载

---

## 构建说明 | Build Guide

### 环境要求
- Qt 5.9+
- Windows (MinGW/MSVC 均可)

### 构建步骤
1. Qt Creator 打开 `PCB_lightgraph.pro`
2. 选择对应 Kit（如 Desktop Qt 5.9 MinGW）
3. 构建并运行


---

## 软件截图与示例 | Screenshots and Examples

软件界面（示例）：

<img width="2559" height="1521" alt="屏幕截图 2026-05-08 212013" src="https://github.com/user-attachments/assets/ea3f0a1f-cd2b-49db-b08f-d1f1839b91fc" />

边缘增强关闭：

![edge_off](https://github.com/tomatorigid/PCB_lightgraph/blob/main/ref_pics/edge_off.png)

边缘增强开启：

![edge_on](https://github.com/tomatorigid/PCB_lightgraph/blob/main/ref_pics/edge_on.png)

参考视频：

[点击观看：以战双帕弥什露西亚为例](https://www.bilibili.com/video/BV1EgAaz2Exx/)

---

## 已知限制与后续计划 | Known Limits and Roadmap

### 已知限制
- 部分极端格式图片仍可能触发第三方解码器差异。
- 大图导入稳定性已增强，但仍受系统内存环境影响。
- 实验性 DP 功能参数尚需更多样本验证。

### 后续计划
- 增加更多工艺/材质模拟参数。
- 持续提升复杂图像边缘处理速度和质量。
- 优化工程包跨平台兼容与版本迁移策略。
- 丰富自动布灯策略与可视化辅助。

---

## 许可证 | License

本项目基于 MIT License 开源。

This project is licensed under the MIT License.

