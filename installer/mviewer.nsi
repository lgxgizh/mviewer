; MViewer.nsi - M11.3 Release Engineering
;
; NSIS installer for MViewer. Expects a pre-deployed application directory
; (produced by scripts/package_portable.ps1 staging: MViewer.exe + Qt DLLs +
; plugins + VC runtime). Run from repo root:
;
;   makensis /DAPP_DIR=dist\staging\MViewer /DVERSION=0.11.0 /DVI_VERSION=0.11.0.0 installer\MViewer.nsi
;
; Output: dist\MViewer-<version>-Setup.exe

!define APPNAME "MViewer"
!ifndef VERSION
  !define VERSION "0.0.0-dev"
!endif
!ifndef VI_VERSION
  !define VI_VERSION "0.0.0.0"
!endif
!ifndef APP_DIR
  !define APP_DIR "dist\staging\MViewer"
!endif
!ifndef OUTFILE
  !define OUTFILE "dist\MViewer-${VERSION}-Setup.exe"
!endif

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${APPNAME}" "InstallDir"

RequestExecutionLevel admin
Unicode True

!include "LogicLib.nsh"
!include "WinMessages.nsh"

VIProductVersion "${VI_VERSION}"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "(c) MViewer contributors"
VIAddVersionKey "FileDescription" "${APPNAME} installer"

Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  ; Deploy the entire pre-built app directory verbatim.
  File /r "${APP_DIR}\*.*"

  ; Start-menu shortcut
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\MViewer.exe"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" "$INSTDIR\Uninstall.exe"

  ; Desktop shortcut
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\MViewer.exe"

  ; ── P2 #⑩: File associations ────────────────────────────────────────
  ; .mviewer workspace file association
  WriteRegStr HKCR ".mviewer" "" "MViewer.Workspace"
  WriteRegStr HKCR "MViewer.Workspace" "" "MViewer Workspace"
  WriteRegStr HKCR "MViewer.Workspace\DefaultIcon" "" "$INSTDIR\MViewer.exe,0"
  WriteRegStr HKCR "MViewer.Workspace\shell\open\command" "" '"$INSTDIR\MViewer.exe" "%1"'

  ; Register common image extensions for Open-With context menu
  !macro AssocExt ext
    WriteRegStr HKCR "MViewer.${ext}" "" "MViewer 图片 (.${ext})"
    WriteRegStr HKCR "MViewer.${ext}\shell\open" "FriendlyAppName" "MViewer"
    WriteRegStr HKCR "MViewer.${ext}\shell\open\command" "" '"$INSTDIR\MViewer.exe" "%1"'
    WriteRegStr HKCR ".${ext}\OpenWithProgids" "MViewer.${ext}" ""
  !macroend
  !insertmacro AssocExt "jpg"
  !insertmacro AssocExt "jpeg"
  !insertmacro AssocExt "png"
  !insertmacro AssocExt "bmp"
  !insertmacro AssocExt "tif"
  !insertmacro AssocExt "tiff"
  !insertmacro AssocExt "webp"
  !insertmacro AssocExt "cr2"
  !insertmacro AssocExt "nef"
  !insertmacro AssocExt "arw"
  !insertmacro AssocExt "dng"
  !insertmacro AssocExt "raf"
  !insertmacro AssocExt "rw2"
  !insertmacro AssocExt "orf"
  !insertmacro AssocExt "raw"
  ; Refresh shell associations
  ${If} ${Silent}
    ; no-op in silent mode
  ${Else}
    System::Call 'shell32.dll::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
  ${EndIf}

  ; Uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "DisplayVersion" "${VERSION}"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  Delete "$DESKTOP\${APPNAME}.lnk"
  ; Remove file associations
  DeleteRegKey HKCR ".mviewer"
  DeleteRegKey HKCR "MViewer.Workspace"
  !macro UnassocExt ext
    DeleteRegKey HKCR "MViewer.${ext}"
    DeleteRegValue HKCR ".${ext}\OpenWithProgids" "MViewer.${ext}"
  !macroend
  !insertmacro UnassocExt "jpg"
  !insertmacro UnassocExt "jpeg"
  !insertmacro UnassocExt "png"
  !insertmacro UnassocExt "bmp"
  !insertmacro UnassocExt "tif"
  !insertmacro UnassocExt "tiff"
  !insertmacro UnassocExt "webp"
  !insertmacro UnassocExt "cr2"
  !insertmacro UnassocExt "nef"
  !insertmacro UnassocExt "arw"
  !insertmacro UnassocExt "dng"
  !insertmacro UnassocExt "raf"
  !insertmacro UnassocExt "rw2"
  !insertmacro UnassocExt "orf"
  !insertmacro UnassocExt "raw"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
  DeleteRegKey HKLM "Software\${APPNAME}"
SectionEnd
