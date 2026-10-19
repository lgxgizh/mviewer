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
// W is Swipe Compare. The same action name is reused when several chords share an action.
inline constexpr CompareShortcut kCompareShortcuts[] = {
    {"Space", "Temporary Swap",
     "With two images, hold to temporarily show the other side under the cursor; release to "
     "restore"},
    {"Esc", "Exit Compare",
     "Clears the selection first if one exists; restores images while Temporary Swap is active"},
    {"Alt+Arrow", "Nudge ROI", "Move the current selection by 1 pixel"},
    {"Shift+Arrow", "Nudge ROI", "Move the current selection by 10 pixels"},
    {"Ctrl+Alt+Arrow", "Resize ROI", "Grow or shrink the selection with the arrow keys"},
    {"B", "Flicker Compare", "Rapidly blink between two images"},
    {"S", "Side-by-Side Split", "Split comparison side by side"},
    {"W", "Swipe Compare", "Swipe along a divider to compare"},
    {"O", "Overlay Compare", "Semi-transparent overlay; Tab toggles the same mode"},
    {"Tab", "Overlay Compare", "Same as O"},
    {"K", "Checkerboard Compare", "Checkerboard alternation between two images"},
    {"H", "Highlight differences",
     "Highlight differing regions in red; similar regions in grayscale"},
    {"Shift+0", "All Color Channels", "Restore full RGB"},
    {"Shift+1", "All Color Channels", "Same as Shift+0 — show RGB"},
    {"Shift+2", "R Channel", "Show the red channel only"},
    {"Shift+3", "G Channel", "Show the green channel only"},
    {"Shift+4", "B Channel", "Show the blue channel only"},
    {"Shift+5", "Y Channel", "Show luminance Y only"},
    {"Shift+6", "V Channel", "Show HSV V only"},
    {"Z", "Sync Zoom", "Synchronize zoom across all panes"},
    {"D", "Synchronized drag", "Synchronize panning across all panes"},
    {"R", "Sync Crosshair", "Show a crosshair at the same image coordinates in every pane"},
    {"L", "Pixel Link", "Click to add marker points and compare RGB"},
    {"I", "Inspector Panel", "Show or hide the analysis side panel"},
    {"F", "Fit window",
     "Fit to the window; whether scale is shared is controlled by Uniform Pixel Scale"},
    {"0", "Fit window", "Same as F"},
    {"Ctrl+0", "Fit window", "Same as F"},
    {"X", "Swap A/B", "Swap the A/B panes"},
    {"Alt+R", "Sync Rotate", "Whether rotate/flip applies to all compared images"},
    {"Ctrl+R", "Rotate Clockwise", "Preview 90° clockwise rotation without writing the file"},
    {"Ctrl+Shift+R", "Rotate Counterclockwise",
     "Preview 90° counterclockwise rotation without writing the file"},
    {"Ctrl+Shift+H", "Flip Horizontal", "Preview horizontal flip without writing the file"},
    {"Ctrl+Shift+V", "Flip Vertical", "Preview vertical flip without writing the file"},
    {"Ctrl+1", "Actual Size", "Display at 100%"},
    {"+ / =", "Zoom In", "Zoom in around the current view center"},
    {"- / _", "Zoom Out", "Zoom out around the current view center"},
    {"Ctrl+Shift+C", "Copy path", "Copy the focused image path"},
    {"Ctrl+C", "Copy to clipboard", "Copy the current compare view to the clipboard"},
    {"Ctrl+S", "Save as PNG…", "Save the current compare view as PNG"},
    {"[", "Lower Diff Threshold", "Decrease the difference threshold by 5"},
    {"]", "Raise Diff Threshold", "Increase the difference threshold by 5"},
    {",", "Lower Overlay Opacity", "Decrease overlay opacity by 5"},
    {".", "Raise Overlay Opacity", "Increase overlay opacity by 5"},
    {"Ctrl+2 / Ctrl+4 / Ctrl+8", "Layout Presets", "Load 2/4/8-pane layout presets"},
    {"1 / 2 / 3 / 4 / 5 / 6 / 7 / 8", "Temporary Image Swap",
     "Hold a digit to temporarily show image N in the pane under the cursor; if not over a pane, "
     "uses the current pane. Out-of-range only shows a hint"},
    {"?", "Shortcut Help", "Open or close this table"},
    {"/", "Shortcut Help", "Same as ?"},
    {"F1", "Shortcut Help", "Same as ?"},
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
