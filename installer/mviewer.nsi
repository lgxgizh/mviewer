; MViewer NSIS installer (M12.3 Release Engineering).
;
; Builds an installable MViewer-<version>-setup.exe that bundles the Qt
; runtime (deployed by windeployqt) INCLUDING the imageformat plugins
; (imageformats/qtiff.dll et al.), so TIFF and other formats work on a clean
; Windows machine with no Qt installed (closes gap G1).
;
; Usage (from repo root, after pack_installer.ps1 stages the deploy dir):
;   makensis installer/mviewer.nsi
;
; The deploy layout expected under DEPLOY_DIR:
;   DEPLOY_DIR/
;     MViewer.exe
;     Qt6Core.dll ... (windeployqt output)
;     imageformats/qtiff.dll ...
;     platforms/qwindows.dll ...
;     ...
; pack_installer.ps1 produces exactly this layout and sets DEPLOY_DIR below.

!define APPNAME "MViewer"
!ifndef APPVERSION
  !define APPVERSION "1.0.0"
!endif
!ifndef DEPLOY_DIR
  !define DEPLOY_DIR "D:\mviewer\build_msvc\bin"
!endif
!ifndef OUTDIR
  !define OUTDIR "D:\mviewer\dist"
!endif

Name "${APPNAME} ${APPVERSION}"
OutFile "${OUTDIR}\${APPNAME}-${APPVERSION}-setup.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
RequestExecutionLevel admin

!include "MUI2.nsh"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetOutPath "$INSTDIR"
  ; Deploy the entire windeployqt layout (exe + Qt DLLs + plugins).
  File /r "${DEPLOY_DIR}\*.*"

  ; Start Menu + Desktop shortcuts.
  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\MViewer.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\MViewer.exe"

  ; Uninstaller.
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
    "DisplayName" "${APPNAME} ${APPVERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
    "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}" \
    "DisplayVersion" "${APPVERSION}"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"
SectionEnd
