set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# MSVC non-standard installation (D:\msvc), rc.exe is under the Windows SDK bin
set(CMAKE_RC_COMPILER "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" CACHE FILEPATH "Resource compiler")

# System include / lib
include_directories(SYSTEM
    "D:/msvc/VC/Tools/MSVC/14.44.35207/include"
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt"
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um"
    "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared"
)
link_directories(
    "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/x64"
    "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/x64"
    "D:/msvc/VC/Tools/MSVC/14.44.35207/lib/x64"
)
