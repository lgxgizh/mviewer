# MViewer v0.9 Beta Checklist

> **用途**：v0.9 Beta 回归门禁。每一个 PR / 每一次功能落地后，由 **UX Review Agent**
> （见下方角色定义）完整跑一遍，全部打勾方可合入。目标是把产品从"开发中的 Demo"
> 推向"可长期使用的产品"。
>
> **M53 release-quality review snapshot (2026-08-29)**
> - M53 focused qualification covers 100MP TIFF Viewer/Compare bounded display,
>   16-bit TIFF plus large PNG/BMP boundaries, Unicode paths, streaming RAW
>   preview scanning, and a five-round large-source lifecycle soak.
> - Final local automated gate: `build.ps1 Test` passed **117/117 twice
>   consecutively** (793.12 s and 792.20 s); `m51_rc_soak` passed in both runs.
> - M53 package contract: **PASS** for the 1.0.13 portable ZIP and installer;
>   physical target-machine UX remains MANUAL/BLOCKED.
> - Automated quality truthfulness: **PASS** means the measured hard-failure
>   count is zero. Advisory complexity warnings are recorded as **accepted
>   baseline debt**, not silently presented as a clean codebase.
> - Automated boundary coverage includes PowerShell 5.1 UTF-8/mojibake,
>   honest responsibility-TU caps, pure source-backed Compare planning, and
>   real MainWindow navigation/Compare adversarial cases.
> - Source-backed large-image truth: **100 MP JPEG and 100MP-class TIFF are
>   implemented** through bounded source-backed display; TIFF uses the measured
>   Windows WIC path, while PNG/BMP retain honest scaled-fallback classification.
> - Native UX review: **MANUAL PENDING / BLOCKED** for physical DPI/ICC/UNC,
>   installer behavior, long-session GUI feel, and extended soak on Windows.
>
> **M57 multi-frame review snapshot (2026-08-29)**
> - Deterministic real-plugin coverage is in place for three-frame GIF, three-frame
>   WebP, and three-page TIFF. The baseline records qgif/qwebp sequential-read
>   behavior and qtiff page random access in
>   `docs/review/M57_PHASE0_MULTIFRAME_BASELINE_2026-08-29.md`.
> - Viewer playback/page navigation is bounded and cancellable; frame/page status,
>   Compare pane identity, workspace restore, metadata, and static export use the
>   selected frame. Preview and thumbnails remain static frame/page zero.
> - Automated closure: two consecutive source-stable Release `build.ps1 Test`
>   gates passed **124/124** in 755.11 s and 753.71 s; architecture violations
>   and complexity hard failures were both zero.
> - UX review is **MANUAL PENDING / BLOCKED** for native playback feel, mixed-DPI
>   zoom continuity, target-machine packaging, hostile disposal fixtures, and
>   long-session/100MP multi-page RSS qualification.
>
> **通过判据（任一不满足即不通过）**：
> - 无等待（no waiting）：每一步操作在预算延迟内完成，不出现可感知的卡顿/转圈。
> - 无闪烁（no flicker）：布局切换、缩放、模式切换时画面不闪、不白屏、不跳动。
> - 无缩放错误（no zoom error）：Fit/100%/200%/自由缩放在任意比例下画面几何正确、未被拉伸或错位。
> - 无状态不同步（no state desync）：目录树、缩略图、主视图、状态栏、工具栏对"当前图片/当前目录"的认知始终一致。

## M44 release-candidate review snapshot (2026-08-15)

- Automated workflow and release gates: **通过** — final Release build and
  full `.\build.ps1 Test` passed **88/88**, including `workflow_ux_tests`,
  Browse/Compare/export acceptance, `golden_image`, `bench_smoke`,
  `bench_enforce`, and async lifetime gates.
- Data-safety and persistence contracts: **通过** — command/file-operation
  regressions cover collision, partial batch, rollback failure, Unicode paths,
  cross-volume fallback, and atomic export/persistence paths.
- Native UX review: **MANUAL PENDING / NOT AVAILABLE** — this run did not have
  an executable native Release GUI review environment. Do not treat the
  offscreen workflow tests as a substitute.
- Pending manual scope: Browse/Compare long-session feel, real 1000+ image
  directory, repeated export/cancel, physical ICC, Windows 100/125/150/200%
  DPI and mixed-DPI movement, real two-volume operations, and native Unicode
  dialogs.
- Full evidence: `docs/review/M44_RELEASE_READINESS_STRUCTURAL_CONVERGENCE_2026-08-15.md`.

## M43 fidelity review snapshot (2026-08-15)

- Automated source/display fidelity: **通过** — `pixelinspector_tests` and
  `compare_acceptance_tests` pass; canonical `build.ps1 Test` is 88/88 in
  three consecutive runs. Coverage includes high-frequency source-vs-LOD
  Inspector RGB, exact 1×1/3×3/5×5/7×7 neighborhoods, LOD replacement
  stability, crop/rotation/adjustment semantics, M42 bounded-memory and
  cancellation/lifetime gates.
- Human UX review: **MANUAL PENDING / NOT AVAILABLE** — native Windows
  mixed-DPI, physical embedded ICC, long-session zoom/flicker feel, and
  hardware RAW16 checks were not performed in the offscreen environment.
- Full evidence: `docs/review/M43_COMPARE_ANALYSIS_FIDELITY_2026-08-15.md`.

---

## 角色：UX Review Agent

- **只评审，不写代码**。职责是检查每一个操作流程是否符合用户直觉。
- 每次 PR 必跑本 Checklist，输出：通过 / 阻塞（附具体步骤与现象）。
- 关注点：
  - 每一个按钮是否必要；每一个操作是否符合用户直觉。
  - 有没有多余点击；有没有状态不同步；有没有默认值不合理。
  - 有没有动画、布局、缩放异常。

---

## Workflow 1 — 浏览主流程

打开目录 → 浏览图片 → 滚轮切换 → 双击放大 → 恢复 → 关闭

| 步骤 | 检查点 | 通过标准 |
|---|---|---|
| 打开目录 | 首屏速度、目录树同步 | 目录树高亮并展开当前目录；首张图在预算内显示 |
| 浏览图片 | 缩略图、主视图 | 缩略图按需解码，无整屏阻塞；主视图即时刷新 |
| 滚轮切换 | 前后切换延迟、状态同步 | 切换无等待、无闪烁；目录树/缩略图焦点跟随；状态栏更新路径与分辨率 |
| 双击放大 | 100% 居中、缩放几何 | 双击在鼠标点处放大到 100%，几何正确、无错位 |
| 恢复 | 回到 Fit | 再次双击/复位回到 Fit，与双击前视图一致 |
| 关闭 | 资源释放 | 关闭后无 GPU/内存泄漏；再次打开无残留状态 |

## Workflow 2 — 比较主流程

选择两张图片 → Compare → 切换（左右 / 覆盖 / 闪烁 / Diff）→ 退出 Compare → 继续浏览

| 步骤 | 检查点 | 通过标准 |
|---|---|---|
| 选择两张 | Ctrl/Shift 多选、框选 | 多选用 `blockSignals` 守卫，无回环；选择序正确 |
| Compare | 全屏、默认布局、自动适配窗口 | 首帧直接全屏；每图按自身窗格 Fit，铺满视口且无空白；窗口缩放时自适应 |
| 切换：左右 | 并排几何 | 左右布局对称，缩放同步 |
| 切换：覆盖 | 叠加对齐 | 两图按相同锚点对齐，无偏移 |
| 切换：闪烁 | 闪烁节奏、无撕裂 | 固定节奏交替，无撕裂/卡顿 |
| 切换：Diff | 差值/热力图 | 默认无染色；显式开启“显示差异”后差值正确、热力图配色可读 |
| 退出 Compare | 回到浏览、状态一致 | 退出后主视图与缩略图选择保持同步，无空白 cell |
| 继续浏览 | 无残留 | 后续切换流畅，比较期间的状态不污染浏览模式 |

---

## 自动化映射（每次 `.\build.ps1 Test` 自动执行）

Checklist 条目凡能自动化的，已固化为回归测试，随门禁自动运行；
其余条目由 UX Review Agent 人工执行。

| Checklist 范围 | 自动化测试（ctest） | 覆盖内容 |
|---|---|---|
| Workflow 1 全流程 | `workflow_ux_tests` | 真实 MainWindow：专业浏览工具栏、Folder/Preview 侧栏比例迁移、Browse 与 Tab 独立恢复、Analysis/Search 跨窗口恢复、路径 Enter 不误开旧图、跨盘精确导航并自动清除冲突目录过滤、目录切换清理旧状态并选中新目录首图、视图模式/固定尺寸/可调滑块状态同步；打开目录 → onImageOpen → ←/→/Home/End/PageUp/PageDown → 首屏解码 ≤ 5 s → Fit/100%/放大/恢复 → 关闭 |
| Workflow 2 全流程 | `workflow_ux_tests` | 真实 MainWindow 全屏 Compare + 真实键鼠事件：默认无 Diff 染色、按窗格 Fit + 相对同步缩放、B/S/O/K/H 模式互斥、Space 临时闪烁、锁定基准、阈值/调整/重置后的指标与 Inspector 同步、拖动期间重计算合并、Esc 退出后继续浏览 |
| Compare linked ROI | `m61_roi_workflow_tests` + `workflow_ux_tests` | Grid/Split/Overlay/Swipe/Checkerboard 真实右键创建，移动与八向缩放，实时镜像，隐藏面板 HUD，TSV 复制，取消/背压/过期结果，以及 decoder bounded-region 真值 |
| Compare 会话恢复 | `compare_session_tests` | 布局/参考图/闪烁间隔/差异阈值 round-trip |
| Compare 引擎语义 | `compare_workflow_tests` | 选择→比较→引擎状态机 |
| 浏览模型 | `product_browse_tests` | 目录列表 / 导航模型 |
| 画质回归 | `golden_image` | PSNR / SSIM / pixel-diff vs `golden/` |
| 性能预算 | `bench_smoke` + `bench_enforce` | Decode / Thumbnail / 预算 ±10% 硬门禁 |
| 人工专项 | —（UX Review Agent） | 闪烁观感、动画流畅度、缩放中心手感、1000 图长时间浏览、内存趋势 |

**规则**：新功能落地时，若触及上述任一 Workflow，必须先扩展
`src/test_workflow_ux.cpp` 的对应断言，再实现功能（先测后写）。
自动化测试通过 ≠ Checklist 通过；人工专项仍需 UX Review Agent 签名。

---

## 分类检查清单

### 浏览体验
- [ ] 打开目录：首屏速度达标（1000 图目录首缩略图 < 2 s）
- [ ] 滚轮浏览：前后切换无等待
- [ ] PageUp / PageDown：翻页正确
- [ ] Home / End：跳到首/末图正确

### Compare
- [ ] Ctrl 多选
- [ ] Shift 多选
- [ ] 框选
- [ ] Compare 默认布局铺满、自动适配窗口
- [ ] 同步缩放
- [ ] 模式切换（左右 / 覆盖 / 闪烁 / Diff）流畅无闪烁
- [ ] 退出 Compare 回到浏览、状态一致

### View
- [ ] Fit
- [ ] 100%
- [ ] 200%
- [ ] 缩放中心（以鼠标点为中心）
- [ ] 平移
- [ ] GIF/WebP：播放、暂停、重启、前后帧、Frame 状态正确
- [ ] 多页 TIFF：前后页导航、Page 状态正确且不改变 Browse 文件索引
- [ ] 帧切换保持缩放/平移，不重新 Fit

### UI
- [ ] 当前目录同步（目录树 ↔ 主视图 ↔ 状态栏）
- [ ] Browse 布局：保留目录/预览，隐藏 Analysis/Search 后画廊无空白或跳动
- [ ] 目录切换：旧缩略图/预览/状态立即清理，新目录首图仅自动选择一次
- [ ] 最近文件
- [ ] 工具栏
- [ ] 状态栏（路径 / 分辨率 / 缩放比 / 帧率）

### 性能
- [ ] GPU Upload
- [ ] Decode
- [ ] Thumbnail
- [ ] Memory（长时间浏览不增长）

### 稳定性
- [ ] 连续打开 1000 张图片
- [ ] 快速切图（无崩溃、无不同步）
- [ ] 快速缩放（无撕裂、无缩放错误）
- [ ] 长时间浏览（内存稳定、无泄漏）
- [ ] 动画快速切换文件后无 stale frame、无残留 timer、无后台无限解码

---

## 签名

| 项 | 值 |
|---|---|
| 评审版本（commit） | |
| UX Review Agent | |
| 结果 | □ 通过 □ 阻塞 |
| 阻塞项 | |
## M45 qualification notes (2026-08-16)

- Automated coverage includes CommandStack callback re-entry, unresolved and
  ordinary command failures, transfer cancellation safety, and clipboard PNG
  generation/lifecycle. The full local Release gate passed **90/90**.
- Delete is currently labeled **MViewer 回收站** and stages files under the
  per-user application data directory. It is not Windows Explorer's native
  Recycle Bin.
- Native Release GUI evidence for ICC, 100/125/150/200 DPI and mixed-DPI
  monitors, two physical volumes, native file dialogs, and long-session soak
  is still **MANUAL / BLOCKED** in the M45 review until target hardware is
  available.
