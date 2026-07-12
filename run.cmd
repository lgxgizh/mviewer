@echo off
setlocal
set PATH=C:\tools\msys64\mingw64\bin;%PATH%
set QT_PLUGIN_PATH=C:\tools\msys64\mingw64\share\qt6\plugins
set QT_QPA_PLATFORM_PLUGIN_PATH=C:\tools\msys64\mingw64\share\qt6\plugins\platforms

cd /d D:\mviewer\build\bin

:: Copy all DLLs from mingw64/bin to current dir
copy C:\tools\msys64\mingw64\bin\Qt6*.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libstdc++-6.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libgcc_s_seh-1.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libwinpthread-1.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libfreetype-6.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libpng16-16.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\zlib1.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libzstd.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libdouble-conversion.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libb2-1.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libicuin78.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libicuuc78.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libpcre2-16-0.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libmd4c.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libharfbuzz-0.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libintl-8.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\libiconv-2.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6Concurrent.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6Network.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6OpenGL.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6OpenGLWidgets.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6PrintSupport.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6Sql.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6Svg.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6SvgWidgets.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6Test.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6UiTools.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6Xml.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6DBus.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6Designer.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6DesignerComponents.dll . >nul 2>&1
copy C:\tools\msys64\mingw64\bin\Qt6Help.dll . >nul 2>&1

echo Starting MViewer...
MViewer.exe 2>&1