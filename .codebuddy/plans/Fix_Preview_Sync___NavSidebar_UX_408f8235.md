---
name: Fix Preview Sync & NavSidebar UX
overview: 修复两个用户反馈的 bug：(1) 单击缩略图时左下角预览面板不刷新（仅多选时才更新）；(2) 左侧导航栏与目录树视觉上看起来像"两个目录树"，需要优化区分度。
todos:
  - id: fix-preview-click-sync
    content: 修复 ThumbnailPanel::mousePressEvent，单击时 setCurrentIndex 触发 Preview 刷新
    status: completed
  - id: guard-preview-stale
    content: 可选：PreviewPanel 异步回调忽略过期 path，避免连点闪烁
    status: completed
    dependencies:
      - fix-preview-click-sync
  - id: clarify-nav-sidebar-ui
    content: 为左侧 NavSidebar 增加「快速导航」标题与样式，区分 DirectoryTree
    status: completed
  - id: verify-build-test
    content: 运行 build.ps1 Test，确认单击/多选/键盘导航与左侧布局正常
    status: completed
    dependencies:
      - fix-preview-click-sync
      - clarify-nav-sidebar-ui
---

