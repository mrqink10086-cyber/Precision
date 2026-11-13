# Precision 2.0.8 + max-su V5

本仓库修订版，基于标准 Precision 2.0.6 与 max-su V5 分支。**本次修复部分武器模型的自动攻击距离和默认轨迹过长问题**，并包含此前全部镜头碰撞、接触点空指针、任务、回调与生命周期修复。

### 下载

在本页 Assets 中选择：

| 文件 | 用途 |
| --- | --- |
| `Precision-2.0.8-maxsu-V5-NG-1597-1170-weapon-bounds.zip` | 玩家安装包，内含更新后的 Precision.dll、说明和许可证 |
| `Precision-2.0.8-maxsu-V5-NG-1597-1170-weapon-bounds-source.zip` | 本构建的对应源码与依赖快照 |
| 两份 `.sha256` | 校验上述安装包与源码包 |

### 本次修复

备用包围球计算曾遗漏几何体的局部旋转。2.0.8 将其改为与常规路径相同的完整局部变换，再应用父级变换，纠正受影响模型的自动长度。

显式长度配置继续优先；无需新增配置。若曾调低倍率补偿旧误差，更新后可重新校准。详细原理见 [武器长度修复](https://github.com/mrqink10086-cyber/Precision/blob/v2.0.8-maxsu-v5-ng-weapon-bounds/docs/WEAPON-BOUNDS.md)。

### 累积内容

- **2.0.7 接触点保护**：对端不是角色时，避免提前访问空 targetActor。
- **2.0.6 镜头过滤**：隔离摄像机与 Precision 的攻击、身体、反弹碰撞层，包括零分组情况。
- **NG 安全与优化**：旧任务取消、失效句柄检查、回调重入和移除同步、碰撞分配回滚、创建 world 保留、异常轨迹和配置保护、固定容量轨迹历史。
- **V5 与 hitstop**：保留 max-su 的自定义命中载荷、MSL 联动和 hitstop 重做，保持公开 V1–V5 API 兼容。

### 安装与兼容

1. 保留原 Precision 2.0.6 的动画、模型、配置等资源。
2. 使用与游戏版本对应的 SKSE、Address Library 和 Microsoft Visual C++ x64 运行库。
3. 安装玩家 ZIP，用 `Data/SKSE/Plugins/Precision.dll` 覆盖原 DLL；MO2 / Vortex 中让本包赢得 DLL 冲突。

同一 DLL 的目标运行时为 **SE 1.5.97 / AE 1.6.1170**。DLL 文件版本为 `2.0.8.0`。之前的 2.0.7、2.0.6 构建可直接替换。

### 验证

- MSVC x64 Release 构建成功，全套 **7/7 CTest 通过**。
- 新增回归覆盖 180° 局部旋转、单位变换、缩放、平移和父子变换顺序；合成样例从错误的 `260` 修正为预期的 `60`。
- DLL 的 AMD64、四个导出、版本和双运行时标记检查通过。
- 安装包与源码包完整性、SHA-256 校验通过；`BUILD-INFO.json` 记录源码提交、依赖、版本与 DLL 哈希。

游戏内回归及反馈中的原始 NIF 尚未验证。上述长度数值来自合成测试。
