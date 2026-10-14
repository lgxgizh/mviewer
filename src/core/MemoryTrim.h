#pragma once

namespace mviewer::core
{

// Best-effort: hand freed heap pages back to the OS after a large working set
// was dropped (Compare pair switch / exit). Windows: _heapmin + trim the
// working set; glibc: malloc_trim(0); no-op elsewhere. Cheap enough for UI
// thread use at a few coarse points; never call per frame.
void releaseFreeHeapToOs();

} // namespace mviewer::core
