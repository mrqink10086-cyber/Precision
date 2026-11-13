# Precision 2.0.8 + max-su V5

基于标准 Precision 2.0.6 与 max-su V5 分支的 CommonLibSSE-NG 合并版，提供自定义命中载荷、MSL 特殊硬直联动和 hitstop 重做，并持续修复碰撞、轨迹与对象生命周期问题。**2.0.8 是本仓库的修订版本号。**

**[下载最新构建](https://github.com/mrqink10086-cyber/Precision/releases/latest)** · [本次更新说明](docs/RELEASE-2.0.8.md) · [所有版本](https://github.com/mrqink10086-cyber/Precision/releases)

## 本次更新

2.0.8 修复部分旋转武器模型的自动攻击距离、默认武器轨迹过长：备用包围球计算现在包含几何体的局部旋转，与常规计算使用相同的完整变换。显式长度配置继续优先。若曾调整倍率补偿旧结果，可在更新后重新校准。原理与回归样例见 [武器长度修复](docs/WEAPON-BOUNDS.md)。

本版本同时包含之前的接触点空指针保护、第三人称镜头碰撞过滤、任务与回调安全修复及运行时优化。

## 下载与安装

| 文件 | 用途 |
| --- | --- |
| `Precision-2.0.8-maxsu-V5-NG-1597-1170-weapon-bounds.zip` | 玩家使用的 DLL 覆盖包 |
| 同名前缀的 `-source.zip` | 对应源码、依赖快照及构建信息 |
| 两份 `.sha256` | 分别校验上述 ZIP |

1. 安装原 Precision 2.0.6，并保留其动画、配置、模型等资源。
2. 使用与游戏版本匹配的 SKSE、Address Library，以及 Microsoft Visual C++ x64 运行库。
3. 安装覆盖包，使其中的 `Data/SKSE/Plugins/Precision.dll` 覆盖原 DLL。MO2 / Vortex 中让本包赢得 DLL 冲突；同一游戏只加载一份 Precision.dll。

同一 x64 DLL 的目标运行时为 **Skyrim SE 1.5.97 / AE 1.6.1170**，VR 构建未启用。公开 V1–V5 API 保持兼容；MSL 1.2.2 可请求 V5。安装本次更新无需增加配置。

## 版本区别

各版按下表自下而上累积，日常安装选择最新版本。

| 版本 | 新增内容 |
| --- | --- |
| [2.0.8 · 武器长度修复](https://github.com/mrqink10086-cyber/Precision/releases/tag/v2.0.8-maxsu-v5-ng-weapon-bounds) | 备用包围球恢复局部旋转，修正受影响模型的自动攻击距离与默认轨迹 |
| [2.0.7 · 接触点空指针修复](https://github.com/mrqink10086-cyber/Precision/releases/tag/v2.0.7-maxsu-v5-ng-null-guard) | 对端不是角色时，避免提前访问空 targetActor |
| [2.0.6 · 镜头碰撞修复](https://github.com/mrqink10086-cyber/Precision/releases/tag/v2.0.6-maxsu-v5-ng-camera-fix) | 隔离摄像机与 Precision 碰撞层，包含此前 NG 合并、优化和安全修复 |

## 验证与技术记录

2.0.8 已通过 MSVC x64 Release 构建、7/7 CTest、DLL 导出和版本元数据检查。新增回归覆盖 180° 局部旋转、缩放、平移及父子变换顺序；合成样例的旧估算为 260，修复后为预期的 60。游戏内回归及反馈中的原始 NIF 尚未验证。

| 文档 | 内容 |
| --- | --- |
| [武器长度](docs/WEAPON-BOUNDS.md) | 本次修复、影响范围与回归测试 |
| [镜头碰撞](docs/CAMERA-COLLISION.md) | 碰撞层过滤、回调优先级及二进制核查 |
| [安全修复](docs/SAFETY-FIXES.md) | 任务重置、回调生命周期、碰撞回收与配置检查 |
| [优化记录](docs/OPTIMIZATION.md) | 差分测试与微基准结果 |
| [合并记录](docs/MERGE.md) | 上游来源、V5 缓存所有权及构建依赖 |

构建使用 Visual Studio 2022、CMake、CommonLibSSE-NG 15.7.3 及 vcpkg 依赖；依赖目录和配置参数见合并记录。配置时增加 `-DPRECISION_BUILD_TESTS=ON` 后，可运行：

```powershell
cmake --build build/ng-merge --config Release --parallel 12
ctest --test-dir build/ng-merge -C Release --output-on-failure
```

## 来源与许可

感谢 [ersh1/Precision](https://github.com/ersh1/Precision)、[max-su-2019/Precision](https://github.com/max-su-2019/Precision) 和 [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)。Havok 相关实现参考 [PLANCK](https://github.com/adamhynek/activeragdoll) 与 [HIGGS](https://github.com/adamhynek/higgs)，调试绘制参考 [SmoothCam](https://github.com/mwilsnd/SkyrimSE-SmoothCam)。

[GPL-3.0-or-later](COPYING) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS)。Modded Code 指 Skyrim 及其变体，Modding Libraries 包括 SKSE、CommonLib 及其变体。发布附件包含对应源码与第三方许可。
