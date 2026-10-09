#pragma once

#include <QString>

#include <cstring>

namespace mviewer::cw
{

// One row of the compare-mode shortcut table. `keys` is the chord shown in the
// help dialog and, when it is a single chord, on the matching control.
struct CompareShortcut
{
    const char *keys;
    const char *name;
    const char *description;
};

// Every key handled by CompareWorkspace::keyPressEvent / keyReleaseEvent.
// W is 滑动对比. The same Chinese name is reused when several chords share an action.
inline constexpr CompareShortcut kCompareShortcuts[] = {
    {"Space", "临时切换", "两张图时按住，鼠标所在一侧临时显示另一侧；松开恢复"},
    {"Esc", "退出比较", "有选区时先清除选区；临时切换中先恢复图像"},
    {"P", "上一对", "上一对图像，PgUp 与 ← 相同"},
    {"PgUp", "上一对", "与 P 相同"},
    {"←", "上一对", "与 P 相同"},
    {"N", "下一对", "下一对图像，PgDn 与 → 相同"},
    {"PgDn", "下一对", "与 N 相同"},
    {"→", "下一对", "与 N 相同"},
    {"Alt+方向键", "微调 ROI", "按 1 像素移动当前选区"},
    {"Shift+方向键", "微调 ROI", "按 10 像素移动当前选区"},
    {"Ctrl+Alt+方向键", "调整 ROI", "按方向键增减选区宽高"},
    {"B", "闪烁对比", "两张图之间快速闪烁"},
    {"S", "左右分割", "左右并排分割对比"},
    {"W", "滑动对比", "沿分割线滑动对比"},
    {"O", "叠加对比", "半透明叠加；Tab 同样切换"},
    {"Tab", "叠加对比", "与 O 相同"},
    {"K", "棋盘对比", "棋盘格交替显示两张图"},
    {"H", "高亮差异", "差异区域红色高亮，相似区域灰度"},
    {"Shift+0", "全色彩通道", "恢复 RGB 全色彩"},
    {"Shift+1", "全色彩通道", "与 Shift+0 相同，显示 RGB"},
    {"Shift+2", "R 通道", "只显示红色通道"},
    {"Shift+3", "G 通道", "只显示绿色通道"},
    {"Shift+4", "B 通道", "只显示蓝色通道"},
    {"Shift+5", "Y 通道", "只显示亮度 Y"},
    {"Shift+6", "V 通道", "只显示 HSV 的 V"},
    {"Z", "同步缩放", "同步所有窗格的缩放倍率"},
    {"D", "同步拖动", "同步所有窗格的平移"},
    {"R", "同步准星", "在各窗格的同一图像坐标显示准星"},
    {"L", "像素连线", "点击添加标记点并比较 RGB"},
    {"I", "检视面板", "展开或收起分析侧栏"},
    {"F", "适合窗口", "按窗口适配；是否统一倍率由「统一像素倍率」决定"},
    {"0", "适合窗口", "与 F 相同"},
    {"Ctrl+0", "适合窗口", "与 F 相同"},
    {"X", "交换 A/B", "对调 A/B 窗格"},
    {"Alt+R", "同步旋转", "旋转与翻转是否作用于全部比较图"},
    {"Ctrl+R", "顺时针旋转", "预览顺时针旋转 90 度，不写入文件"},
    {"Ctrl+Shift+R", "逆时针旋转", "预览逆时针旋转 90 度，不写入文件"},
    {"Ctrl+Shift+H", "水平翻转", "预览水平翻转，不写入文件"},
    {"Ctrl+Shift+V", "垂直翻转", "预览垂直翻转，不写入文件"},
    {"Ctrl+1", "实际大小", "按 100% 显示"},
    {"+ / =", "放大", "以当前视图为中心放大"},
    {"- / _", "缩小", "以当前视图为中心缩小"},
    {"Ctrl+Shift+C", "复制路径", "复制焦点图像的路径"},
    {"Ctrl+C", "复制到剪贴板", "把当前比较视图复制到剪贴板"},
    {"Ctrl+S", "保存为 PNG…", "把当前比较视图保存为 PNG"},
    {"[", "降低差异阈值", "差异阈值减 5"},
    {"]", "提高差异阈值", "差异阈值加 5"},
    {",", "降低叠加透明度", "叠加透明度减 5"},
    {".", "提高叠加透明度", "叠加透明度加 5"},
    {"Ctrl+2 / Ctrl+4 / Ctrl+8", "布局预设", "按 2、4、8 宫格载入布局"},
    {"1 / 2 / 3 / 4 / 5 / 6 / 7 / 8", "临时换图",
     "按住数字在鼠标所在窗格临时显示第 N 张；不在窗格上则用当前窗格。超出张数仅提示"},
    {"?", "快捷键帮助", "打开或关闭本表"},
    {"/", "快捷键帮助", "与 ? 相同"},
    {"F1", "快捷键帮助", "与 ? 相同"},
};

inline const CompareShortcut *findCompareShortcut(const char *keys)
{
    if (!keys)
        return nullptr;
    for (const CompareShortcut &entry : kCompareShortcuts)
    {
        if (entry.keys && std::strcmp(entry.keys, keys) == 0)
            return &entry;
    }
    return nullptr;
}

inline QString compareShortcutLabel(const char *name, const char *keys)
{
    const char *safeName = name ? name : "";
    const char *safeKeys = keys ? keys : "";
    return QString::fromUtf8(safeName) + QString::fromUtf8(" (") + QString::fromUtf8(safeKeys) +
           QString::fromUtf8(")");
}

inline QString compareShortcutText(const char *keys)
{
    const CompareShortcut *entry = findCompareShortcut(keys);
    if (!entry)
        return QString::fromUtf8(keys ? keys : "");
    return compareShortcutLabel(entry->name, entry->keys);
}

} // namespace mviewer::cw
