#pragma once

#include <string>

#include "domain/Project.h"

namespace mviewer {
namespace core {

// Serialize a Project to a .mvproj JSON document. The embedded Workspace is
// serialized with serializeWorkspace and stored under the "workspace" key, so a
// .mvproj is fully self-contained.
std::string serializeProject(const domain::Project &p);

// Parse a .mvproj JSON document into a Project. Returns false on malformed
// input or a missing / invalid embedded workspace.
bool deserializeProject(const std::string &json, domain::Project &out);

} // namespace core
} // namespace mviewer
