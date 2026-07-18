# MViewer Plugin Specification

## Overview

The plugin system provides extensibility without modifying the core application. Plugins can add new image formats, metadata handlers, UI panels, and export capabilities. The system is designed to be lightweight, versioned, and isolated from the core browsing performance.

**Status:** Post-MVP. The plugin system is not required for version 0.1 but the architecture must accommodate it.

---

## Design Goals

1. **Non-intrusive** — Plugins cannot degrade core browsing performance
2. **Versioned** — ABI stability across minor versions
3. **Discoverable** — Automatic plugin detection and loading
4. **Isolated** — Plugin failures do not crash the application
5. **Simple** — Minimal boilerplate for plugin authors

---

## Plugin Types

### 1. Image Decoder Plugins

Add support for new image formats or improve existing decoders.

```cpp
class IDecoderPlugin : public IPlugin {
public:
    /// Plugin interface
    virtual PluginInfo info() const = 0;

    /// Decoder capabilities
    virtual std::vector<ImageFormat> supportedFormats() const = 0;
    virtual float priority() const { return 0.5f; }  // 0.0-1.0, higher = preferred

    /// Decode operations
    virtual std::expected<DecodedImage, DecodeError>
        decode(const FilePath& path, const DecodeParams& params) = 0;

    virtual std::expected<DecodedImage, DecodeError>
        decodeThumbnail(const FilePath& path, int maxEdgeLength) = 0;

    /// Animation support (optional)
    virtual bool supportsAnimation() const { return false; }
};
```

### 2. Metadata Handler Plugins

Add support for new metadata formats or custom tag extraction.

```cpp
class IMetadataPlugin : public IPlugin {
public:
    virtual PluginInfo info() const = 0;

    /// Which metadata standards this plugin handles
    virtual std::vector<MetadataFormat> supportedFormats() const = 0;

    /// Parse metadata from file
    virtual std::expected<Metadata, MetadataError>
        parse(const FilePath& path) = 0;

    /// Parse from raw buffer (for embedded metadata)
    virtual std::expected<Metadata, MetadataError>
        parseBuffer(std::span<const std::byte> data) = 0;
};
```

### 3. UI Extension Plugins

Add toolbar buttons, menu items, or panel content.

```cpp
class IUIExtensionPlugin : public IPlugin {
public:
    virtual PluginInfo info() const = 0;

    /// Toolbar contributions
    virtual std::vector<ToolbarItem> toolbarItems() const = 0;

    /// Menu contributions
    virtual std::vector<MenuItem> menuItems() const = 0;

    /// Panel contributions (docked widgets)
    virtual std::vector<PanelDefinition> panels() const = 0;

    /// Context menu contributions
    virtual std::vector<ContextMenuAction> contextMenuActions() const = 0;
};
```

### 4. Export Filter Plugins

Add output formats for batch conversion.

```cpp
class IExportPlugin : public IPlugin {
public:
    virtual PluginInfo info() const = 0;

    /// Supported output formats
    virtual std::vector<std::string> supportedExtensions() const = 0;

    /// Export a single image
    virtual std::expected<void, ExportError>
        exportImage(const DecodedImage& image, const FilePath& outputPath,
                    const ExportParams& params) = 0;

    /// Batch export (optional optimization)
    virtual std::expected<BatchResult, ExportError>
        exportBatch(const std::vector<ExportTask>& tasks,
                    const ExportParams& params) = 0;
};
```

---

## Plugin Interface

### Base Interface

All plugins implement `IPlugin`:

```cpp
class IPlugin {
public:
    virtual ~IPlugin() = default;

    /// Plugin metadata
    virtual PluginInfo info() const = 0;

    /// Lifecycle
    virtual bool initialize(IHost* host) = 0;
    virtual void shutdown() = 0;

    /// State
    virtual bool isEnabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
};
```

### Plugin Info

```cpp
struct PluginInfo {
    std::string id;              // Unique identifier (e.g., "mviewer.heif-decoder")
    std::string name;            // Display name
    std::string description;     // Short description
    std::string author;          // Author/organization
    std::string version;         // Plugin version (semver)
    int apiVersion;              // MViewer plugin API version
    std::string website;         // URL for more info
    std::string license;         // License identifier (MIT, GPL, etc.)
};
```

### Host Interface

The host provides services to plugins:

```cpp
class IHost {
public:
    /// Logging
    virtual void log(LogLevel level, const std::string& message) = 0;

    /// Settings
    virtual std::optional<std::string> getSetting(const std::string& key) = 0;
    virtual void setSetting(const std::string& key, const std::string& value) = 0;

    /// Cache access (read-only)
    virtual std::shared_ptr<const DecodedImage>
        findCachedImage(const CacheKey& key) = 0;

    /// UI access
    virtual void showMessage(const std::string& title, const std::string& text) = 0;
    virtual void addStatusIndicator(const std::string& id, const std::string& text) = 0;

    /// Event subscription
    virtual void subscribe(EventType event, EventCallback callback) = 0;
    virtual void unsubscribe(EventType event, CallbackId id) = 0;
};
```

---

## Plugin Discovery

### Search Paths

| Platform | Path |
| ---------- | ------ |
| Windows | `%APPDATA%\MViewer\plugins\` |
| Windows | `<install_dir>\plugins\` |
| Linux | `~/.local/share/mviewer/plugins/` |
| Linux | `/usr/lib/mviewer/plugins/` |
| All | Directory specified by `MVIEWER_PLUGIN_PATH` env var |

### Discovery Process

1. Scan all search paths for `.dll` (Windows) or `.so` (Linux) files
2. Load each library and query entry point: `mviewer_plugin_create()`
3. Validate `apiVersion` matches current MViewer API version
4. Call `initialize(host)` — if it returns false, skip plugin
5. Register plugin capabilities with appropriate subsystem
6. Store plugin metadata for UI display

### Entry Point

```cpp
// C linkage for ABI stability
extern "C" {

/// Create plugin instance
__declspec(dllexport)  // Windows
// __attribute__((visibility("default")))  // Linux
IPlugin* mviewer_plugin_create();

/// Destroy plugin instance
__declspec(dllexport)
void mviewer_plugin_destroy(IPlugin* plugin);

/// Query API version supported by this plugin
__declspec(dllexport)
int mviewer_plugin_api_version();

} // extern "C"
```

---

## Plugin Lifecycle

```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ Discover │───▶│  Load    │───▶│ Initialize│───▶│  Active  │
└──────────┘    └──────────┘    └──────────┘    └──────────┘
                                                      │
                                                      ▼
                                               ┌──────────┐
                                               │ Shutdown │
                                               └──────────┘
                                                      │
                                                      ▼
                                               ┌──────────┐
                                               │  Unload  │
                                               └──────────┘
```

### States

| State | Description |
| ------- | ------------- |
| Discovered | Found in plugin directory, not yet loaded |
| Loaded | Library loaded, entry point resolved |
| Initialized | `initialize()` called successfully |
| Active | Registered and operational |
| Disabled | User or system disabled the plugin |
| Error | Failed to load or initialize |

### Lifecycle Events

| Event | When | Plugin Action |
| ------- | ------ | --------------- |
| `OnLoad` | Library loaded | Allocate resources |
| `OnInitialize` | `initialize()` called | Register capabilities |
| `OnEnable` | User enables plugin | Activate functionality |
| `OnDisable` | User disables plugin | Deactivate, keep state |
| `OnShutdown` | Application closing | Release resources |
| `OnUnload` | Library unloading | Final cleanup |

---

## Plugin API Versioning

### Version Scheme

- **API Version:** Integer, incremented on breaking changes
- **Current API Version:** 1 (initial)
- Plugins declare which API version they target
- MViewer supports current and one previous API version

### Compatibility

| MViewer Version | API Version | Supports Plugins Targeting |
| ---------------- | ------------- | --------------------------- |
| 1.0.x | 1 | 1 |
| 1.1.x | 1 | 1 |
| 2.0.x | 2 | 1, 2 |
| 2.1.x | 2 | 1, 2 |

### Breaking Changes Policy

- Breaking changes only in major versions
- Deprecation warnings in minor versions before removal
- Migration guide provided for each API version bump

---

## Plugin Security

### Current Model (In-Process)

Plugins run in the same process as MViewer. This provides maximum performance but requires trust.

**Implications:**

- A misbehaving plugin can crash the application
- Plugins have access to the same memory space
- No sandboxing (by design, for performance)

### Mitigations

1. **Validation** — Verify plugin API version and entry points before loading
2. **Isolation** — Plugin crashes caught via SEH (Windows) / signal handlers (Linux)
3. **Permissions** — Plugin declares required capabilities; user approves
4. **Signing** — Future: plugin signature verification

### Future: Out-of-Process (Optional)

For untrusted plugins, an out-of-process execution model may be added:

- Plugin runs in separate process
- IPC via shared memory + message queue
- Higher latency but better isolation
- Suitable for export filters and batch operations
- Not suitable for decode plugins (latency-sensitive)

---

## Plugin Configuration

### Settings Namespace

Each plugin gets its own settings namespace:

```json
{
    "plugins": {
        "mviewer.heif-decoder": {
            "enabled": true,
            "settings": {
                "hardwareAcceleration": true
            }
        }
    }
}
```

### UI Integration

- Plugin settings appear in the main Settings dialog
- Each plugin can provide a settings widget
- Settings are per-plugin, namespaced by plugin ID

---

## Example Plugin: HEIF Decoder

```cpp
// heif_decoder_plugin.cpp
#include <mviewer/plugin.h>
#include <libheif/heif.h>

class HeifDecoderPlugin : public IDecoderPlugin {
public:
    PluginInfo info() const override {
        return {
            .id = "mviewer.heif-decoder",
            .name = "HEIF/HEIC Decoder",
            .description = "Decode HEIF and HEIC images via libheif",
            .author = "MViewer Team",
            .version = "1.0.0",
            .apiVersion = MVIEWER_PLUGIN_API_VERSION,
            .website = "https://github.com/mviewer/plugins",
            .license = "MIT",
        };
    }

    bool initialize(IHost* host) override {
        m_host = host;
        return true;
    }

    void shutdown() override {
        // Cleanup
    }

    std::vector<ImageFormat> supportedFormats() const override {
        return {ImageFormat::HEIF, ImageFormat::HEIC};
    }

    float priority() const override { return 0.8f; }

    std::expected<DecodedImage, DecodeError>
    decode(const FilePath& path, const DecodeParams& params) override {
        // libheif decode implementation
    }

    std::expected<DecodedImage, DecodeError>
    decodeThumbnail(const FilePath& path, int maxEdgeLength) override {
        // Fast thumbnail decode
    }

    bool supportsAnimation() const override { return false; }

    bool isEnabled() const override { return m_enabled; }
    void setEnabled(bool enabled) override { m_enabled = enabled; }

private:
    IHost* m_host = nullptr;
    bool m_enabled = true;
};

extern "C" {

IPlugin* mviewer_plugin_create() {
    return new HeifDecoderPlugin();
}

void mviewer_plugin_destroy(IPlugin* plugin) {
    delete plugin;
}

int mviewer_plugin_api_version() {
    return MVIEWER_PLUGIN_API_VERSION;
}

} // extern "C"
```

---

## Plugin Distribution

### Official Plugins

- Maintained by MViewer core team
- Distributed with the application
- Located in `<install_dir>/plugins/`

### Community Plugins

- Third-party maintained
- Distributed via GitHub releases or plugin registry
- User installs by copying to plugin directory
- Future: built-in plugin manager for discovery and updates

### Plugin Registry (Future)

- Central repository of community plugins
- Search and install from within MViewer
- Rating and review system
- Automatic update notifications

---

## Performance Requirements

| Requirement | Target |
| ------------- | -------- |
| Plugin discovery | < 50ms |
| Plugin load | < 100ms |
| Plugin initialization | < 200ms |
| Decode plugin overhead | < 1ms per call |
| Plugin crash isolation | No application crash |
| Memory overhead per plugin | < 10MB baseline |

### Constraints

- Plugins must not block the UI thread
- Decode plugins must be stateless (thread-safe)
- Plugin calls must have bounded execution time
- Long-running operations (export, batch) must report progress

---

## API Version History

### Version 1 (Initial)

- `IDecoderPlugin` — Image format support
- `IMetadataPlugin` — Metadata format support
- `IUIExtensionPlugin` — UI contributions
- `IExportPlugin` — Export/conversion support
- `IHost` — Logging, settings, cache read, UI messaging
- Plugin discovery via filesystem scanning
- In-process execution only

### Future Versions

- `IImageProcessorPlugin` — Pixel-level filters (future)
- `ICollectionPlugin` — Virtual folder sources (future)
- `IAutomationPlugin` — Scripting/automation hooks (future)
- Out-of-process execution option
- Plugin signing and verification
