# NG 安全修复

本轮以 `8fb061968e14724e144be3008698e4083fbf0e67` 为修复基线，使用提供的 `CommonLibSSE-NG-ng` 15.7.3 源码。未修改 NG 库本身，保留 Precision 2.0.6 版本及 V1–V5 的公开虚函数布局。

同一 DLL 保留 SE 1.5.97 与 AE 1.6.1170：构建启用 `ENABLE_SKYRIM_SE=ON`、`ENABLE_SKYRIM_AE=ON`，旧版 Query 允许 1.5.97，版本导出的兼容清单显式列出两者。VR 未启用。新增 `bhkWorld::GetWorld1()` 的 slot `0x27` 已在两版实际可执行文件核对，均为 `mov rax,[rcx+0x10]; ret`，与 NG 的 `referencedObject` 声明一致；数据和文件 SHA256 见 `audit/world-vtable.json`。原新增命中 hook 和 cinfo 槽位核对见 `audit/hit-hook.json`。

## 任务与游戏对象

`ApplyHitImpulse` 同时验证 handle 的解析结果与刚体参数；延迟执行时确认刚体仍属于当前引用的 3D 树。handle 的非零值不再被当作解析成功的依据。`ForEachRagdollDriver` 与刚体版 clone 查询也处理空输入。

四个任务队列共享单调递增的 epoch。奇数表示重置中，拒绝新任务和新一轮执行；加锁前后的 epoch 必须相同，因此清理前已开始等待锁的请求也不能在清理后进入新世界。`Clear` 串行化重置，逐个等待活动队列阶段结束、释放旧任务，再清理场景状态；不同时持有多个队列锁，保留 post-Havok 到 pre-physics 的原有调度关系。清理 weapon mesh 缓存也使用其写锁。

## 轨迹

粒子创建失败、模型类型错误、缺少 `TrailRoot`、空节点等路径会安全结束该轨迹。历史只保留最新四个 transform，保留原四样本预热与 Catmull–Rom 插值的取样顺序。

`particleObject` 尚未建立时仍允许异步克隆继续。NG 只声明克隆任务指针，没有完成或失败状态接口；不能据此把暂时为空判为永久失败。历史在等待期间也保持固定容量。

## 回调与接口

九类回调统一使用注册表：派发缓存的不可变名单，在注册表锁外调用原 `std::function` 对象，保留可变 lambda 状态。增删时更新名单，普通派发不逐次分配。移除先停用，尚未开始的调用会跳过；外部调用等待在途调用及捕获对象析构结束。回调或捕获对象析构中的移除不等待，以避免重入等待环；调用方须让所有在途调用结束后再卸载代码。公开 API 注释记录此规则，并明确 clone 查询返回借用指针。

PostHit 名单在 PreHit 回调之后、`HitData_Populate` 和伤害处理之前捕获。仅在名单非空时准备 HitData，伤害之后执行同一名单。因此 PreHit 内新增的 PostHit 可以接收当前命中；伤害处理期间新增的订阅从后续命中开始，移除的条目则跳过。这保留无订阅者路径原本的引擎调用行为。

## 碰撞与配置

碰撞节点分配失败会返回失败；recoil 节点失败保留已经成功的主碰撞。shape、刚体和 collision object 构造后由局部 `NiPointer` 管理；后续分配失败时反向释放临时拥有权，成功时由节点、body 和 Havok 引用继续持有。失败节点会分离，胶囊数据只在提取成功后使用。

碰撞实例保留创建时的 `bhkWorld`，清理不再依赖 actor handle 或 actor 当前所在 cell。移除时持该 world 的锁，确认刚体仍属于该 world 后注销，再分离节点。NG 的 `NiPointer`、`NiRefObject`、`hkRefPtr` 及 `GetWorld1()` 声明均已核对；未增加运行时地址或修改 Havok 结构布局。

TOML 的必填 NodeName、已提供的碰撞字段和向量分量均检查解析结果；缺失可选字段保留缺省值，保留原 toml++ 数值转换行为。数组元素和 Conditions/Visuals 等 table 也先检查类型。错误条目记录字段诊断并跳过，不解引用空 optional/table。

## 验证与审查

- MSVC x64 Release 插件构建成功；原 V1–V5 声明、callback aliases 与版本枚举核对通过。
- 五组 CTest 全部通过：原 runtime 差分、队列 epoch/reset、回调生命周期/快照/并发、四样本历史、TOML 字段边界。回调测试还检查外部移除等待捕获析构、析构重入、异常计数恢复和预热后派发无分配。
- 上述五组分别通过 MSVC AddressSanitizer 和 Clang UBSan trap，合计十次独立测试运行。
- 三个独立 Luna xhigh 复审覆盖并发、内存/栈与游戏指针/业务顺序；最终固定提交的结果另记 `SAFETY-REVIEW.md`。

可复现基础检查：

```powershell
cmake --build build/ng-merge --config Release --target Precision precision_runtime_tests precision_job_queue_tests precision_callback_tests precision_trail_history_tests precision_config_tests --parallel 12
ctest --test-dir build/ng-merge -C Release --output-on-failure
```

独立 sanitizer 使用 `/std:c++20 /O1 /EHsc /MD /Zi`，MSVC 追加 `/fsanitize=address`，Clang 追加 `/clang:-fsanitize=undefined /clang:-fsanitize-trap=undefined`；include 路径为 `src` 及实际 toml++ include 目录。分别编译运行 `tests/{runtime,job_queue,callback,trail_history,config}_tests.cpp`。

游戏内尚未验证。上述 sanitizer 覆盖独立 helper；引擎分配失败、actor 卸载、读档/world 切换、NIF 克隆和实际 Hook 栈状态由源码/NG 契约复审，仍需游戏回归。原先 Hook 地址与结构布局保持不变。
