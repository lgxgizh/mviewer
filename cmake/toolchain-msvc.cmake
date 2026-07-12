set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# MSVC 非标准安装 (D:\msvc)，rc.exe 在 Windows SDK bin 下
set(CMAKE_RC_COMPILER "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe" CACHE FILEPATH "Resource compiler")

# 系统 include / lib
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
