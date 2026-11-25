#pragma once

#include <exception>
#include <string>
#include <utility>

// C++ exception boundary for one plugin callback.
//
// A native plugin is mapped into this process. try/catch here stops a C++
// exception from escaping into the host. It does not stop access violations,
// abort, stack exhaustion, or any other non-C++ fault, and it is not a
// security sandbox or an isolation boundary.

namespace mviewer::core
{

template <typename Fn>
auto invokePluginCallback(const char *what, std::string &error, bool &ok, Fn &&fn) -> decltype(fn())
{
    using Result = decltype(fn());
    try
    {
        ok = true;
        return std::forward<Fn>(fn)();
    }
    catch (const std::exception &ex)
    {
        ok = false;
        error = std::string(what) + " threw: " + ex.what();
    }
    catch (...)
    {
        ok = false;
        error = std::string(what) + " threw";
    }
    return Result{};
}

template <typename Fn> bool invokePluginVoid(const char *what, std::string &error, Fn &&fn)
{
    try
    {
        std::forward<Fn>(fn)();
        return true;
    }
    catch (const std::exception &ex)
    {
        error = std::string(what) + " threw: " + ex.what();
    }
    catch (...)
    {
        error = std::string(what) + " threw";
    }
    return false;
}

} // namespace mviewer::core
