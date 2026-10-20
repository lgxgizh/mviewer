#include "core/async/CooperativeCancel.h"

namespace mviewer::core
{

const ScopedCancelProbe *&ScopedCancelProbe::current()
{
    static thread_local const ScopedCancelProbe *top = nullptr;
    return top;
}

} // namespace mviewer::core
