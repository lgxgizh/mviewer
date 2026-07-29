#include "core/perf/GpuTracker.h"

#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <dxgi.h>
#include <windows.h>
#endif

namespace mviewer::perf
{

GpuSnapshot sampleGpu()
{
    GpuSnapshot s;
#ifdef _WIN32
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInit = SUCCEEDED(co);

    IDXGIFactory1 *factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory));
    if (SUCCEEDED(hr) && factory)
    {
        IDXGIAdapter *adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            if (!adapter)
                break;
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                s.dedicatedVideoBytes += desc.DedicatedVideoMemory;
                s.dedicatedSystemBytes += desc.DedicatedSystemMemory;
                s.sharedBytes += desc.SharedSystemMemory;
                s.available = true;
            }
            adapter->Release();
            adapter = nullptr;
        }
        factory->Release();
    }

    if (comInit)
        CoUninitialize();
#else
    (void)s;
#endif
    return s;
}

} // namespace mviewer::perf
