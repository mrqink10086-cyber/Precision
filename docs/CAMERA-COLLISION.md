# 第三人称摄像机碰撞修复

针对贴近角色的第三人称镜头，在大范围挥扫动作中可能被 Precision 武器碰撞体触发避障、产生突然拉近或晃动的问题。

## 改动

`HavokHooks::CompareFilterInfo` 显式排除摄像机层 36/39 与 Precision 攻击、身体、反弹层 56/57/58 的配对。过滤对参数顺序对称，且不依赖 system group 或层掩码。原有七处过滤 hook 共用此入口，没有新增 hook、运行时地址、分配或对象缓存。

公开过滤回调仍先执行；第三方回调显式返回 Collide/Ignore 时继续尊重其结果。关闭 Precision 时仍返回原版过滤路径。地形、墙体、门、普通角色与摄像机的碰撞，以及武器命中、反弹和 character controller 规则不受此项过滤影响。

CollisionLayer 的原值完整移至独立 `CollisionFilter.h`，供生产代码和无引擎测试共用。公开 V1–V5 API、配置格式和 DLL 2.0.6 版本保持原样；本次用独立发布标签区分构建。

## 定位依据

原有默认 Precision 掩码已经排除了摄像机层，但创建碰撞体时继承到零 system group 的值没有被拒绝。SE 1.5.97 与 AE 1.6.1170 的原版过滤函数在任一侧分组为零时直接允许碰撞，早于层矩阵检查。摄像机查询后续的玩家归属排除分支也不包含自定义层 56–58。

过滤函数 RVA 分别为 `0xdd6a80`、`0xeb6c50`；零组放行分支分别为 `0xdd6c1b`、`0xeb6dc6`。独立执行这两个完整叶函数，在两种运行时、摄像机 36/39、Precision 56/57/58、零组/非零组及正反参数顺序上比较，共 48 次均确认零组可绕过默认掩码。

[SmoothCam 的 CastRay](https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/master/SmoothCam/source/raycast.cpp) 同样调用引擎 CameraCaster 做相机避障，因此该引擎路径也适用于使用此接口的 SmoothCam 镜头。

## 验证

- MSVC x64 Release 插件构建通过；六组 CTest 全部通过。
- 新增 `precision_camera_filter_tests`，覆盖全部 128×128 层配对、两侧各三个分组值及四种标记，共 2,359,296 个组合。只排除上述 12 个有序层配对，其余层交给原路径；零组、高位分组与标记均不改变配对判定。
- DLL 检查确认 AMD64、原四个导出、版本 2.0.6，以及 SE 1.5.97 / AE 1.6.1170 两个兼容运行时标记。

```powershell
cmake -S . -B build/ng-merge -DPRECISION_BUILD_TESTS=ON
cmake --build build/ng-merge --config Release --target Precision precision_camera_filter_tests --parallel 12
ctest --test-dir build/ng-merge -C Release --output-on-failure
```

构建目录沿用 `MERGE.md` 中的依赖配置。游戏内尚未实测，也未捕获反馈现场的 filterInfo；本修复关闭已验证的过滤缺口，不将机器码实验等同于原反馈已经复现。游戏回归应检查近距离镜头下的横扫空挥、贴墙避障、正常命中和反弹；若第三方过滤回调主动放行摄像机配对，其覆盖仍然生效。
