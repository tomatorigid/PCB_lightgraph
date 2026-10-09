<p align="center">
    <img src="logo_rounded.png" alt="PCB_lightgraph" width="200">
</p>

<h1 align="center">PCB_lightgraph</h1>

<div align="center">
    <img alt="Language" src="https://img.shields.io/badge/Language-C%2B%2B17-%2300599C?logo=cplusplus&logoColor=white">
    <img alt="Framework" src="https://img.shields.io/badge/Framework-Qt%206.5.3-%2341CD52?logo=qt&logoColor=white">
    <br>
    <img alt="Platform" src="https://img.shields.io/badge/platform-Windows-blueviolet">
    <img alt="License" src="https://img.shields.io/badge/license-MIT-%23D22128">
</div>

<div align="center">
    <img alt="stars" src="https://img.shields.io/github/stars/tomatorigid/PCB_lightgraph?style=social">
    <img alt="GitHub all releases" src="https://img.shields.io/github/downloads/tomatorigid/PCB_lightgraph/total?style=social">
</div>

<br>

<p align="center">
PCB_lightgraph是一个将 2D 插画<br>一键转换为可制造 PCB 分层图纸的高性能桌面工具<br>
实时预览，图片无需预处理。
</p>

### PCB_lightgraph交流QQ群：[点击加入群聊](https://qm.qq.com/q/w8af77CnDi) **734946967**

# 亮点功能
1. **多层自动拆分**：自动拆为铜层/阻焊/丝印/背透光四层，实现**单面5色+透光n色**（阻焊色支持白/蓝/黑/红/绿/黄/紫，白色自动联动反色）
2. **灯光设计**：自动（重心建议）或手动布灯，灯光范围实时预览，散射半径与不透明度可调
3. **边缘处理双模式**：边缘增强（拉普拉斯）/ 描边（Canny），共享阈值统一调参
4. **工程文件**：一键保存/打开 `*.pcblg` 工程包
5. **图像实时联动**：`File -> 画图实时编辑`，画图 `Ctrl+S` 后自动重载
6. **FluentUI3 界面**：WinUI3 深色风格 + 圆角图标；参数分组点击标题展开/收起（带动画），控制台支持滚轮/触摸滚动
7. **渐进式渲染**：滑块拖动按「自适应预览 → 可选细化 → 原图」渐进渲染，大图调参流畅不卡顿
8. **界面缩放与屏幕适配**：`Option -> 界面缩放` 75%~150% 全局缩放（无需重启），启动自动适配屏幕，小屏友好
9. **多种表面工艺**：沉金 / 喷锡 / **OSP 玫瑰金（#F0AA93）**，金属像素按工艺匹配，导出自动命名 ENIG/HASL/OSP
10. **自定义色值**：`Option -> 颜色设置` 可调沉金/OSP/喷锡/裸露基材及蓝/黑/红/绿/白/黄/紫七种阻焊层显示色值（本地持久化，与工程无关）；`重置所有设置` 一键恢复默认
11. **高性能**：使用 **C++** 进行编写，给你极致的反应速度
12. **选区调参**：支持矩形、套索、画笔和魔棒选区，独立调整区域内的 9 项参数，支持撤销重做与工程保存恢复


### 一键自动金色沉金勾线，单面5色，随意调节，实时预览，图片可无需预处理。(此示例无预处理)

![alt text](ref_pics/image_release.png)



# 相关视频教程

- [PCB艺术画制作 速通【教程】 v1.1.1](https://www.bilibili.com/video/BV1bJRbBeEW9/)
- [从零开始的二次元电路板艺术画设计 v1.2.0](https://www.bilibili.com/video/BV1CQjz6ZELZ/)
- [PCB灯光画以战双帕弥什露西亚为例](https://www.bilibili.com/video/BV1EgAaz2Exx/)


## 快速上手

1. 从[RELEASE](https://github.com/tomatorigid/PCB_lightgraph/releases)下载本软件最新版本，解压缩
2. 打开程序（双击压缩包内exe），`File -> 导入图片`（无需预处理）
3. 点击分组标题展开参数（基础参数/灯光/边缘操作等，控制台可滚动）
4. 视需求展开「边缘操作」分组，选择描边或增强
5. （可选）展开「灯光」分组使用自动/手动布灯预览效果
6. `File -> 导出图纸`，或 `保存工程 (.pcblg)` 中途保存


## 技术要点

### 1. 图像分层核心
基于阈值与颜色/亮度规则，将输入图像自动拆分为 PCB 制造所需层，统一流程输出四层图。
表面工艺支持沉金 / 喷锡 / OSP（玫瑰金 #F0AA93），金属像素按所选工艺的色相匹配（色值可在「颜色设置」中自定义）。

### 2. 边缘增强与描边
- 边缘增强：使用拉普拉斯算子强化轮廓与细节边界
- 描边（Canny Stroke）：稳定的线条提取，适合插画轮廓
- 自动反色范围：按局部环境决定边缘色，减轻明暗背景对比不足

### 3. 高斯预滤波
可在边缘处理前降噪去杂点，可开关，提升边缘处理稳定性。

### 4. Douglas-Peucker 路径抽稀（实验性）
对轮廓路径抽稀简化、减少冗余点，支持容忍度与线宽参数。

### 5. 自动布灯建议
基于图像分布与权重给出布灯候选点，支持手动覆盖调整。

## 性能、兼容性与优化处理

- 边缘计算缓存化：避免重复计算，边缘模式切换与反复预览响应迅速
- 渐进式渲染：滑块拖动时按「自适应预览 → 可选细化 → 原图」三阶段逐帧渲染，预览分辨率随图片尺寸自适应，大图操作不卡顿
- 大图高稳定导入：像素上限预判与自动缩放，降低崩溃风险
- 图片格式强兼容：文件头检测，处理后缀与真实编码不一致的图片
- C++底层，性能强悍，内存占用少



## 作者及感谢

**作者**：<br>
[@芙ling痛恨数学分析](https://space.bilibili.com/549252923) 独立开发并持续维护本软件
<br>
**感谢**：  
[@御坂10297号](https://space.bilibili.com/454466365) 制作教学视频并分享项目  
[@Laplac_heroin](https://space.bilibili.com/3461564136950176) 优化软件使用体验  
[FluentUI3Style](https://github.com/XHY-ChuJian/FluentUIStyle)（作者 [@XHY-ChuJian](https://github.com/XHY-ChuJian)，MIT License）—— 本软件 Fluent/WinUI3 界面风格基于该项目，内嵌于 `third_party/FluentUI3Style/`，其授权许可见该目录下 `LICENSE`

[@Laplac_heroin](https://space.bilibili.com/3461564136950176) 移植本软件为Web便携版[PCB_lightgraph_Portable](https://github.com/Ashlixy17/PCB_lightgraph_Portable) 

[@G100wasd](https://github.com/G100wasd)移植本软件为嘉立创EDA插件[PCB_lightgraph_Extension](https://github.com/G100wasd/PCB_lightgraph_Extension)

### 如果觉得软件对你有帮助，帮忙点个 Star 吧！~（网页最上方右上角的小星星），这就是对我们最大的支持了！

## Star History

<a href="https://www.star-history.com/?repos=tomatorigid%2FPCB_lightgraph&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=tomatorigid/PCB_lightgraph&type=date&theme=dark&legend=top-left&sealed_token=QNFNJNGUJKfKO5lynTvy4Xinwg9Yh7bzuUnlw3iSACeBt--ZSlbPpRMhwx9JgCGHuynTjOXSWDip844r2HoepvIPZFmCFb01n4MKGWEUSfksLCMbLoVceUVb9_GTp7O2lFr9QK_KqM3qcU1m-RZdXCTt-3mvgfcyYBhH-39-UmUGAA7-FFxGUpgMpTUU" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=tomatorigid/PCB_lightgraph&type=date&legend=top-left&sealed_token=QNFNJNGUJKfKO5lynTvy4Xinwg9Yh7bzuUnlw3iSACeBt--ZSlbPpRMhwx9JgCGHuynTjOXSWDip844r2HoepvIPZFmCFb01n4MKGWEUSfksLCMbLoVceUVb9_GTp7O2lFr9QK_KqM3qcU1m-RZdXCTt-3mvgfcyYBhH-39-UmUGAA7-FFxGUpgMpTUU" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=tomatorigid/PCB_lightgraph&type=date&legend=top-left&sealed_token=QNFNJNGUJKfKO5lynTvy4Xinwg9Yh7bzuUnlw3iSACeBt--ZSlbPpRMhwx9JgCGHuynTjOXSWDip844r2HoepvIPZFmCFb01n4MKGWEUSfksLCMbLoVceUVb9_GTp7O2lFr9QK_KqM3qcU1m-RZdXCTt-3mvgfcyYBhH-39-UmUGAA7-FFxGUpgMpTUU" />
 </picture>
</a>

## 许可证

本项目代码采用 [MIT License](https://opensource.org/licenses/MIT) 开源，您可以自由使用、修改、分发，仅需保留版权声明与许可证文本。

> 代码本身许可宽松，但本软件产出的图纸与成品画面属于二次创作范畴，其合规性取决于您所使用的原始素材，与代码许可无关。

---

### 声明

本软件是一款将 2D 插画转换为 PCB 分层图纸的开源工具（以下简称"本工具"），主要面向学习、研究与个人创作。

1. **素材版权由使用者负责**：本工具不会为输入图像做版权审查。若您使用他人的原创作品（插画、角色形象、图片等）进行制作，请确保已获得相应授权；因素材侵权产生的纠纷与责任由使用者自行承担，与作者及本工具无关。
2. **肖像与隐私**：请勿使用本工具处理涉及他人肖像、隐私或敏感内容的图像。
3. **商用免责**：若有商家/商贩使用本工具进行 PCB 周边产品的生产与销售，由此产生的产品质量、商业模式、版权纠纷及其他一切问题与后果，均与作者及本工具无关，作者不承担任何责任。
4. **按现状提供（AS-IS）**：本工具按现状提供，不附带任何明示或暗示的担保，作者不对其适用性、可靠性或特定用途作出任何保证。
5. 使用本工具即视为已阅读并同意以上内容。
