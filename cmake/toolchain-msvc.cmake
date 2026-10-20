# Optional manual toolchain file for configuring outside a vcvars shell
# (cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-msvc.cmake). build.ps1 and CI do
# NOT use it: they load the MSVC environment through vcvars64.bat.
#
# Paths come from the environment vcvars/VsDevCmd exports, so the file works on
# any machine with a standard Visual Studio + Windows SDK install:
#   VCToolsInstallDir   e.g. C:/Program Files/Microsoft Visual Studio/2022/.../MSVC/14.44.35207/
#   WindowsSdkDir       e.g. C:/Program Files (x86)/Windows Kits/10/
#   WindowsSDKVersion   e.g. 10.0.26100.0\
# Each can also be set as a cache variable (-DMVIEWER_MSVC_TOOLS_DIR=... etc.).
# The original dev-box layout (D:/msvc, SDK 10.0.26100.0) is only the last-resort
# fallback when neither is provided.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Picks the first non-empty value of: cache variable, environment variable,
# legacy fallback. Normalizes to forward slashes without a trailing separator.
function(_mviewer_toolchain_path out_var cache_var env_var fallback)
    if(DEFINED ${cache_var} AND NOT "${${cache_var}}" STREQUAL "")
        set(_value "${${cache_var}}")
    elseif(NOT "$ENV{${env_var}}" STREQUAL "")
        set(_value "$ENV{${env_var}}")
    else()
        set(_value "${fallback}")
    endif()
    file(TO_CMAKE_PATH "${_value}" _value)
    string(REGEX REPLACE "/+$" "" _value "${_value}")
    set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

_mviewer_toolchain_path(_mv_vc_tools MVIEWER_MSVC_TOOLS_DIR VCToolsInstallDir
    "D:/msvc/VC/Tools/MSVC/14.44.35207")
_mviewer_toolchain_path(_mv_sdk_root MVIEWER_WINDOWS_SDK_DIR WindowsSdkDir
    "C:/Program Files (x86)/Windows Kits/10")
_mviewer_toolchain_path(_mv_sdk_ver MVIEWER_WINDOWS_SDK_VERSION WindowsSDKVersion
    "10.0.26100.0")

if(NOT EXISTS "${_mv_vc_tools}/include")
    message(WARNING "toolchain-msvc: MSVC include dir not found under '${_mv_vc_tools}'. "
                    "Run from a vcvars64 shell or set MVIEWER_MSVC_TOOLS_DIR.")
endif()
if(NOT EXISTS "${_mv_sdk_root}/Include/${_mv_sdk_ver}")
    message(WARNING "toolchain-msvc: Windows SDK ${_mv_sdk_ver} not found under '${_mv_sdk_root}'. "
                    "Set WindowsSdkDir/WindowsSDKVersion or MVIEWER_WINDOWS_SDK_DIR/_VERSION.")
endif()

# rc.exe lives under the versioned Windows SDK bin directory.
set(CMAKE_RC_COMPILER "${_mv_sdk_root}/bin/${_mv_sdk_ver}/x64/rc.exe" CACHE FILEPATH
    "Resource compiler")

# System include / lib
include_directories(SYSTEM
    "${_mv_vc_tools}/include"
    "${_mv_sdk_root}/Include/${_mv_sdk_ver}/ucrt"
    "${_mv_sdk_root}/Include/${_mv_sdk_ver}/um"
    "${_mv_sdk_root}/Include/${_mv_sdk_ver}/shared"
)
link_directories(
    "${_mv_sdk_root}/Lib/${_mv_sdk_ver}/um/x64"
    "${_mv_sdk_root}/Lib/${_mv_sdk_ver}/ucrt/x64"
    "${_mv_vc_tools}/lib/x64"
)
