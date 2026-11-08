# Precision 2.0.6 + max-su V5

## 合并范围

以标准 Precision 2.0.6（`9ef45e0d8d9e3cf8b2df444f05142c1e7bf41cd4`）为基础，合入 max-su 分支（`4b9dad01fad2a815231a6e6e8624700aecc9bbd9`）相对于共同祖先 `df3cd228795bf32288de795dc6eb3b38e46abf34` 的全部源码增量。

- 保留标准 2.0.6 的骨骼索引、CFilter、Havok world 和运行时结构访问修复。
- 保留标准 API V1–V4 的虚函数顺序、签名和命中数据布局，追加原作者 V5 的四个方法。
- 合入自定义碰撞注释、extra-data 命中缓存及作者的 hitstop 重做。
- 使用本地 CommonLibSSE-NG 15.7.3（`07262cc2ae75777a422ed0c9621310fadef7c382`）构建，目标游戏为 Steam 1.6.1170。SE/AE 编译开关开启，VR 关闭。

三方预合并没有文本冲突。18 个 fork 改动文件中，`vcpkg.json` 和 `src/CMakeLists.txt` 的 fork 增量已存在于 2.0.6；其余 16 个文件合入。之后的构建适配与缓存修正另见下文。

## 构建适配

最新 CommonLib 已定义 Precision 早期自行补齐的九个 Havok 类型，因此采用库中定义并核对大小、字段位置、枚举值和构造行为。`bhkRigidBodyCinfo` 的 Havok 部分在库中是原始字节：保留 16 字节栈对齐，仅按已知成员偏移写入需要修改的字段，其余构造器初始化内容保持原样。没有修改 CommonLib 本身。

移除已不再提供的 `SKSEAPI` 宏，插件版本元数据明确标记 1.6.1170，并使用 NG 的更新结构标记。保留插件版本 2.0.6；安装包以 `maxsu-V5` 后缀区分标准版。

glm 固定为原 submodule 提交 `586a402397dd35d66d7a079049856d1e2cbab300`，tomlplusplus 固定为 v3.4.0。缺少本地依赖时 CMake 可拉取；可用 `FETCHCONTENT_SOURCE_DIR_*` 指定本地目录。

## V5 缓存所有权

原 fork 在释放读锁后返回命中缓存内部指针。本次将读取改为锁内复制：

- `GetCachedHitData` 返回线程本地命中快照，直到同线程下一次成功调用或线程退出有效；所有指针字段（包括游戏对象和 Havok 刚体）仍是借用引用，可能早于快照失效，不能跨对应对象的生命周期保存。
- 两个 `GetCachedExtraData` 重载共享线程本地字符串；直到同线程下一次成功调用或线程退出有效。缓存替换或删除不会使该字符串失效。
- 注册的自定义参数名由 Precision 保存字符串副本。

这些改动保持 V1–V5 虚函数 ABI。MSL 1.2.2 已请求 V5 并立即消费载荷，不需要重编。

## 安装与验证边界

这是 DLL 覆盖包，需要原 Precision 2.0.6 的动画、配置和其他资源。用包内 `Data/SKSE/Plugins/Precision.dll` 覆盖原 DLL；同一游戏只加载一个 Precision.dll。保留原 MSL 资源和配置。

新增 HitEventHook 已在实际 1.5.97、1.6.1170 可执行文件副本中核对：`37673 + 0x3C0` / `38627 + 0x4A8` 是对应的 E8 调用，目标 ID 为 `37633` / `38586`，参数准备为 Actor*、HitData*。这不是对全部 Precision 原有地址的重新审计。

尚未启动游戏验证。进入 1.6.1170 后，应确认 SKSE 成功加载 Precision、MSL 日志取得 V5，并测试普通命中、带 MSL 注释的特殊硬直、多目标、击杀与 hitstop，以及读档后表现。

## 构建记录

本次使用 MSVC 19.44、Windows SDK 10.0.26100.0、x64 Release 和动态 MSVC CRT。依赖包通过已有的 vcpkg 安装前缀提供；下列本地路径可替换为相应依赖的安装或源码目录。

```powershell
cmake -S . -B build/ng-merge -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
  -DCMAKE_PREFIX_PATH=E:/source/repos/valhallaCombat/build/vcpkg_installed/x64-windows-static-md `
  -DCOMMONLIBSSE_NG_PATH=E:/source/repos/CommonLibSSE-NG-ng `
  -DFETCHCONTENT_SOURCE_DIR_HDE64=E:/source/repos/valhallaCombat/build/_deps/hde64-src `
  -DFETCHCONTENT_SOURCE_DIR_GLM=E:/source/repos/Precision-main/build/deps/glm `
  -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS=E:/source/repos/Precision-main/build/deps/tomlplusplus `
  -DCOMMONLIB_ENABLE_IPO=OFF
cmake --build build/ng-merge --config Release --target Precision --parallel 12
```

构建与链接通过。`tools/check_api.py` 对比固定版本的标准版和 fork 头文件，确认 13 个标准声明、回调类型别名及 V5 虚函数声明一致；仅允许 fork 新增的无布局影响默认构造函数。新增命中调用点和 cinfo 基类虚表槽位的实际二进制记录见 `docs/audit/hit-hook.json`。验证没有覆盖游戏运行中的 Havok 行为。

## 来源

- [标准 2.0.6](https://github.com/ersh1/Precision/tree/9ef45e0d8d9e3cf8b2df444f05142c1e7bf41cd4)
- [max-su 分支](https://github.com/max-su-2019/Precision/tree/4b9dad01fad2a815231a6e6e8624700aecc9bbd9)
- [标准分支相对共同祖先](https://github.com/ersh1/Precision/compare/df3cd228795bf32288de795dc6eb3b38e46abf34...9ef45e0d8d9e3cf8b2df444f05142c1e7bf41cd4)
