# PCB_lightgraph
PCB_lightgraph is a specialized CAD utility that converts illustrations into multi-layered PCB manufacturing files (Copper, Mask, Silk, Bottom-Mask). It features real-time light-scattering simulation and intelligent multi-color LED placement for creating light-guided PCB artwork.



[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Qt](https://img.shields.io/badge/Framework-Qt%205.x-blue.svg)](https://www.qt.io/)

## 📖 简介 | Introduction
**PCB_lightgraph** 是一款专为艺术家和硬件工程师设计的工具，用于将 2D 插画转换为可制造的 PCB 透光艺术画。  
**PCB_lightgraph** is a high-performance utility designed for artists and hardware engineers to transform 2D illustrations into manufacturable PCB light-guide artwork.

---

## ✨ 核心特性 | Key Features

* **多层自动化提取 | Multi-Layer Extraction**
    * **Top Copper (线路层)**: 用于表现金属质感或导电路径。 | For metallic textures or conductive paths.
    * **Top Mask (阻焊层)**: 定义表面工艺（沉金/喷锡）的露铜区域。 | Defines exposed copper areas for surface finishes (ENIG/HASL).
    * **Top Silk (丝印层)**: 高对比度的图形层。 | High-contrast graphic layer.
    * **Bottom Mask (透光层)**: 在基材背面开窗，实现背光透射效果。 | Creates light-transmitting windows in the substrate back.

* **智能灯条布局 | Intelligent LED Placement**
    * **色彩聚类算法 | Hue-Clustering**: 自动分析画面色彩分布，确保灯光颜色与原图匹配。 | Analyzes color distribution to ensure multi-color LED variety.
    * **重心探测 | Centroid Detection**: 根据画面亮度和色彩权重自动建议灯条位置。 | Automatically suggests LED positions based on brightness and color weight.

* **高性能仿真 | High-Performance Simulation**
    * **实时渲染 | Real-time Rendering**: 使用行指针 (Scanline) 优化算法，支持大尺寸图像的流畅调参。 | Uses Scanline optimization for smooth real-time parameter adjustment.
    * **物理光效 | Physics-based Falloff**: 基于分段距离算法模拟光线在 FR4 基材中的散射衰减。 | Simulates light scattering using segment-distance attenuation.

---
软件界面
![示例图片1](https://github.com/tomatorigid/PCB_lightgraph/blob/main/ref_pics/show1.png)

导出示例（未自动布灯）
![示例图片2](https://github.com/tomatorigid/PCB_lightgraph/blob/main/ref_pics/show2.png)

---
## 🛠️ 核心控制 | Core Controls

| 功能 | Feature | 描述 | Description |
| :--- | :--- | :--- | :--- |
| **表面处理** | **Surface Finish** | 切换沉金 (亮金) 与喷锡 (银色) 的渲染。 | Toggle between ENIG (Gold) and HASL (Silver). |
| **阻焊颜色** | **Mask Color** | 模拟不同颜色的 PCB 油墨（蓝、红、黑、白）。 | Simulates PCB ink colors (Blue, Red, Black, White). |
| **布灯敏感度** | **LED Sensitivity** | 控制自动生成灯条的数量 (0-10)。 | Controls the number of automatically generated LEDs. |
| **散射半径** | **Scattering Radius** | 调整模拟光晕的大小。 | Adjusts the light halo size. |

---

## 🚀 使用指南 | Usage Guide

0. **下载软件 | Download**: 从Release下载最新发布版本 | Download the latest release version from ‘Release’.
1. **导入图片 | Import**: 加载您的目标插画文件。 | Load your target illustration file.
2. **调整阈值 | Adjust**: 通过滑动条定义金属、丝印和透光区域。 | Define metal, silk, and transmissive areas via sliders.
3. **布局灯条 | Place LEDs**: 使用“自动布灯”或手动在预览图上点击拖拽。 | Use "Auto-Suggest" or drag on preview to place LEDs manually.
4. **导出文件 | Export**: 导出符合 EDA 导入标准的单色反转位图。 | Save EDA-ready inverted monochrome bitmaps.

---

## 🔨 构建说明 | Build Instructions

* **环境要求 | Requirements**: Qt 5.9+ (MinGW / MSVC).
* **构建步骤 | Steps**:
    1. 使用 Qt Creator 打开 `PCB_lightgraph.pro`。 | Open `PCB_lightgraph.pro` in Qt Creator.
    2. 选择合适的构建套件并运行。 | Select a kit and Run.

---

## 📜 许可证 | License

本项目采用 MIT 许可证开源。 | This project is licensed under the MIT License.
