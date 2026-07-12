#!/bin/bash
# MViewer 启动脚本
export PATH=/mingw64/bin:/d/mviewer/build/bin:/usr/bin:/bin:$PATH
cd /d/mviewer/build/bin

# 复制所有依赖 DLL
cp /mingw64/bin/Qt6*.dll .
cp /mingw64/bin/libstdc++-6.dll .
cp /mingw64/bin/libgcc_s_seh-1.dll .
cp /mingw64/bin/libwinpthread-1.dll .
cp /mingw64/bin/libfreetype-6.dll .
cp /mingw64/bin/libpng16-16.dll .
cp /mingw64/bin/zlib1.dll . 2>/dev/null
cp /mingw64/bin/libzstd.dll . 2>/dev/null
cp /mingw64/bin/libdouble-conversion.dll . 2>/dev/null
cp /mingw64/bin/libb2-1.dll . 2>/dev/null
cp /mingw64/bin/libicuin78.dll . 2>/dev/null
cp /mingw64/bin/libicuuc78.dll . 2>/dev/null
cp /mingw64/bin/libpcre2-16-0.dll . 2>/dev/null
cp /mingw64/bin/libmd4c.dll . 2>/dev/null
cp /mingw64/bin/libharfbuzz-0.dll . 2>/dev/null
cp /mingw64/bin/libintl-8.dll . 2>/dev/null
cp /mingw64/bin/libiconv-2.dll . 2>/dev/null

# 复制 plugins
mkdir -p platforms imageformats
cp /mingw64/share/qt6/plugins/platforms/*.dll platforms/
cp /mingw64/share/qt6/plugins/imageformats/*.dll imageformats/

# 运行
./MViewer.exe
    
