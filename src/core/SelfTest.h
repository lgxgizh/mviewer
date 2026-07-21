#pragma once

namespace mviewer::core
{

// P5: headless release self-test gate.
//
// Generates a known image, decodes it through the DecoderRegistry, and verifies
// the decode/metadata roundtrip. This is the single command a release pipeline
// runs to prove the core decode path is intact before shipping. Invoked by
// `mviewer --selftest`. Returns 0 on success, non-zero on failure.
int runSelfTest();

} // namespace mviewer::core
