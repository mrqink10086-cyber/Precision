# 2.0.7：接触点回调的空角色解引用

日期：2026-11-11。基线：`feec34cf25787ce38f0c6cdc7ee90a7fe8fae695`（2.0.6 + max-su V5 camera fix）。

## 问题

`ContactListener::ContactPointCallback` 在处理接触点时无条件调用 `targetActor->GetHandle()`：

```cpp
auto targetActorHandle = targetActor->GetHandle();

// while in combat, filter out actors like teammates etc.
if (targetActor && attackerActor && precisionHandler->CheckActorInCombat(attackerHandle)) {
```

同一函数在别处都按 `target` / `targetActor` 可能为空来写（`target ? target->GetHandle() : RE::ObjectRefHandle()`、`if (targetActor)`、`if (targetActor && targetActor->IsGhost())`），只有这一行没有判空。攻击判定打到地形、静态碰撞体，或接触对端解析不出角色时，`targetActor` 就是空指针，这一行会读 `null` 的 vtable。

崩溃是否发生取决于对端有没有角色：纯地形命中里 `targetActor` 为空，必崩；角色命中时不为空，不会触发。因此这是「可能的问题」而不是必然复现的崩溃。

## 修复

`targetActorHandle` 只在该分支内使用，把它移进 `if (targetActor && attackerActor && ...)` 里即可：

```cpp
// while in combat, filter out actors like teammates etc.
if (targetActor && attackerActor && precisionHandler->CheckActorInCombat(attackerHandle)) {
    auto targetActorHandle = targetActor->GetHandle();
```

分支结束后没有任何地方再引用 `targetActorHandle`，所以移动不改变语义：命中角色时的队友、召唤物、非敌对战斗目标过滤逻辑完全保留；`targetActor` 为空时跳过整段战斗过滤，与它上方「self filter」的处理一致。

## 影响范围

- 只影响接触点回调里战斗过滤分支的进入条件，不改命中判定、伤害计算、拖尾、命中特效或摄像机碰撞。
- 插件版本从 `2.0.6` 升到 `2.0.7`（`0x2000070`），双运行时标记 `1.5.97` / `1.6.1170`、四个导出、V1–V5 API 均不变。
- 配置格式、co-save、资源文件不变；覆盖 DLL 即可。

## 验证

- Release 构建通过（MSVC 19.44 x64 `/MD`）。
- `ctest --test-dir build/ng-merge -C Release` **6/6 通过**：runtime、job_queue、trail_history、callback、config、camera_filter。
- PE 检查：AMD64、版本 `2.0.7.0`、插件版本 `0x2000070`、兼容运行时 `0x1050610` / `0x1064920`、导出 `RequestPluginAPI`、`SKSEPlugin_Load`、`SKSEPlugin_Query`、`SKSEPlugin_Version`。
- 未做游戏内实测。空指针路径的验证依赖代码审查：这一行在 `targetActor` 为空时必然读空，移动后不再有该访问。
