#include "core/analyzer/AnalyzerPipeline.h"

// AnalyzerPipeline is a thin, header-only orchestration facade over
// AnalyzerRegistry. All behavior lives inline in the header; this translation
// unit exists only so the symbol is compiled into mviewer_core and exported
// for consumers linking against the shared library.
