#pragma once

#include <QCoreApplication>
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
// W is Swipe. The same action name is reused when several chords share an action.
inline constexpr CompareShortcut kCompareShortcuts[] = {
    {"Space", QT_TRANSLATE_NOOP("CompareShortcuts", "Peek"),
     QT_TRANSLATE_NOOP(
         "CompareShortcuts",
         "With two images, hold to temporarily show the other side under the cursor; release to "
         "restore")},
    {"Esc", QT_TRANSLATE_NOOP("CompareShortcuts", "Exit"),
     QT_TRANSLATE_NOOP(
         "CompareShortcuts",
         "Clears the selection first if one exists; restores images while Peek is active")},
    {"Alt+Arrow", QT_TRANSLATE_NOOP("CompareShortcuts", "Nudge ROI"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Move the current selection by 1 pixel")},
    {"Shift+Arrow", QT_TRANSLATE_NOOP("CompareShortcuts", "Nudge ROI"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Move the current selection by 10 pixels")},
    {"Ctrl+Alt+Arrow", QT_TRANSLATE_NOOP("CompareShortcuts", "Resize ROI"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Grow or shrink the selection with the arrow keys")},
    {"B", QT_TRANSLATE_NOOP("CompareShortcuts", "Flicker"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Rapidly blink between two images")},
    {"S", QT_TRANSLATE_NOOP("CompareShortcuts", "Split"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Split comparison side by side")},
    {"W", QT_TRANSLATE_NOOP("CompareShortcuts", "Swipe"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Swipe along a divider to compare")},
    {"O", QT_TRANSLATE_NOOP("CompareShortcuts", "Overlay"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Semi-transparent overlay; Tab toggles the same mode")},
    {"Tab", QT_TRANSLATE_NOOP("CompareShortcuts", "Overlay"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Same as O")},
    {"K", QT_TRANSLATE_NOOP("CompareShortcuts", "Checkerboard"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Checkerboard alternation between two images")},
    {"H", QT_TRANSLATE_NOOP("CompareShortcuts", "Highlight Diff"),
     QT_TRANSLATE_NOOP("CompareShortcuts",
                       "Highlight differing regions in red; similar regions in grayscale")},
    {"Shift+0", QT_TRANSLATE_NOOP("CompareShortcuts", "All Color Channels"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Restore full RGB")},
    {"Shift+1", QT_TRANSLATE_NOOP("CompareShortcuts", "All Color Channels"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Same as Shift+0 — show RGB")},
    {"Shift+2", QT_TRANSLATE_NOOP("CompareShortcuts", "R Channel"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Show the red channel only")},
    {"Shift+3", QT_TRANSLATE_NOOP("CompareShortcuts", "G Channel"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Show the green channel only")},
    {"Shift+4", QT_TRANSLATE_NOOP("CompareShortcuts", "B Channel"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Show the blue channel only")},
    {"Shift+5", QT_TRANSLATE_NOOP("CompareShortcuts", "Y Channel"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Show luminance Y only")},
    {"Shift+6", QT_TRANSLATE_NOOP("CompareShortcuts", "V Channel"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Show HSV V only")},
    {"Z", QT_TRANSLATE_NOOP("CompareShortcuts", "Sync Zoom"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Synchronize zoom across all panes")},
    {"D", QT_TRANSLATE_NOOP("CompareShortcuts", "Synchronized drag"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Synchronize panning across all panes")},
    {"R", QT_TRANSLATE_NOOP("CompareShortcuts", "Sync Crosshair"),
     QT_TRANSLATE_NOOP("CompareShortcuts",
                       "Show a crosshair at the same image coordinates in every pane")},
    {"L", QT_TRANSLATE_NOOP("CompareShortcuts", "Pixel Link"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Click to add marker points and compare RGB")},
    {"I", QT_TRANSLATE_NOOP("CompareShortcuts", "Inspector"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Show or hide the analysis side panel")},
    {"F", QT_TRANSLATE_NOOP("CompareShortcuts", "Fit window"),
     QT_TRANSLATE_NOOP(
         "CompareShortcuts",
         "Fit to the window; whether scale is shared is controlled by Uniform Pixel Scale")},
    {"0", QT_TRANSLATE_NOOP("CompareShortcuts", "Fit window"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Same as F")},
    {"Ctrl+0", QT_TRANSLATE_NOOP("CompareShortcuts", "Fit window"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Same as F")},
    {"X", QT_TRANSLATE_NOOP("CompareShortcuts", "Swap A/B"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Swap the A/B panes")},
    {"Alt+R", QT_TRANSLATE_NOOP("CompareShortcuts", "Sync Rotate"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Whether rotate/flip applies to all compared images")},
    {"Ctrl+R", QT_TRANSLATE_NOOP("CompareShortcuts", "Rotate Clockwise"),
     QT_TRANSLATE_NOOP("CompareShortcuts",
                       "Preview 90° clockwise rotation without writing the file")},
    {"Ctrl+Shift+R", QT_TRANSLATE_NOOP("CompareShortcuts", "Rotate Counterclockwise"),
     QT_TRANSLATE_NOOP("CompareShortcuts",
                       "Preview 90° counterclockwise rotation without writing the file")},
    {"Ctrl+Shift+H", QT_TRANSLATE_NOOP("CompareShortcuts", "Flip Horizontal"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Preview horizontal flip without writing the file")},
    {"Ctrl+Shift+V", QT_TRANSLATE_NOOP("CompareShortcuts", "Flip Vertical"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Preview vertical flip without writing the file")},
    {"Ctrl+1", QT_TRANSLATE_NOOP("CompareShortcuts", "Actual Size"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Display at 100%")},
    {"+ / =", QT_TRANSLATE_NOOP("CompareShortcuts", "Zoom In"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Zoom in around the current view center")},
    {"- / _", QT_TRANSLATE_NOOP("CompareShortcuts", "Zoom Out"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Zoom out around the current view center")},
    {"Ctrl+Shift+C", QT_TRANSLATE_NOOP("CompareShortcuts", "Copy path"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Copy the focused image path")},
    {"Ctrl+C", QT_TRANSLATE_NOOP("CompareShortcuts", "Copy to clipboard"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Copy the current compare view to the clipboard")},
    {"Ctrl+S", QT_TRANSLATE_NOOP("CompareShortcuts", "Save as PNG…"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Save the current compare view as PNG")},
    {"[", QT_TRANSLATE_NOOP("CompareShortcuts", "Lower Diff Threshold"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Decrease the difference threshold by 5")},
    {"]", QT_TRANSLATE_NOOP("CompareShortcuts", "Raise Diff Threshold"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Increase the difference threshold by 5")},
    {",", QT_TRANSLATE_NOOP("CompareShortcuts", "Lower Overlay Opacity"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Decrease overlay opacity by 5")},
    {".", QT_TRANSLATE_NOOP("CompareShortcuts", "Raise Overlay Opacity"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Increase overlay opacity by 5")},
    {"Ctrl+2 / Ctrl+4 / Ctrl+8", QT_TRANSLATE_NOOP("CompareShortcuts", "Layout Presets"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Load 2/4/8-pane layout presets")},
    {"1 / 2 / 3 / 4 / 5 / 6 / 7 / 8", QT_TRANSLATE_NOOP("CompareShortcuts", "Temporary Image Swap"),
     QT_TRANSLATE_NOOP("CompareShortcuts",
                       "Hold a digit to temporarily show image N in the pane under the cursor; if "
                       "not over a pane, "
                       "uses the current pane. Out-of-range only shows a hint")},
    {"?", QT_TRANSLATE_NOOP("CompareShortcuts", "Shortcut Help"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Open or close this table")},
    {"/", QT_TRANSLATE_NOOP("CompareShortcuts", "Shortcut Help"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Same as ?")},
    {"F1", QT_TRANSLATE_NOOP("CompareShortcuts", "Shortcut Help"),
     QT_TRANSLATE_NOOP("CompareShortcuts", "Same as ?")},
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

// Shortcut names and descriptions are English source strings in the
// "CompareShortcuts" translation context; translate them at display time.
inline QString compareShortcutTr(const char *text)
{
    return text ? QCoreApplication::translate("CompareShortcuts", text) : QString();
}

inline QString compareShortcutLabel(const char *name, const char *keys)
{
    const char *safeKeys = keys ? keys : "";
    return compareShortcutTr(name) + QString::fromUtf8(" (") + QString::fromUtf8(safeKeys) +
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
