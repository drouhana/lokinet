set(GUI_ZIP_URL "https://builds.lokinet.dev/loki-project/loki-network-control-panel/master/lokinet-gui-windows-32bit-v0.3.2.zip")
set(GUI_ZIP_HASH SHA256=136595c287f0664e02a7c62a88fd69299510545b0f25c028dba0142f62b8e189)
set(TUNTAP_URL "https://build.openvpn.net/downloads/releases/latest/tap-windows-latest-stable.exe")
set(TUNTAP_EXE "${CMAKE_BINARY_DIR}/tuntap-install.exe")
set(BOOTSTRAP_URL "https://seed.lokinet.org/lokinet.signed")
set(BOOTSTRAP_FILE "${CMAKE_BINARY_DIR}/bootstrap.signed")

file(DOWNLOAD
    ${TUNTAP_URL}
    ${TUNTAP_EXE})

file(DOWNLOAD
    ${BOOTSTRAP_URL}
    ${BOOTSTRAP_FILE})

file(DOWNLOAD
    ${GUI_ZIP_URL}
    ${CMAKE_BINARY_DIR}/lokinet-gui.zip
    EXPECTED_HASH ${GUI_ZIP_HASH})

execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf ${CMAKE_BINARY_DIR}/lokinet-gui.zip
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

install(DIRECTORY ${CMAKE_BINARY_DIR}/gui DESTINATION share COMPONENT gui)
install(PROGRAMS ${TUNTAP_EXE} DESTINATION bin)
install(FILES ${BOOTSTRAP_FILE} DESTINATION share)

set(CPACK_PACKAGE_INSTALL_DIRECTORY "Lokinet")
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/win32-setup/lokinet.ico")
set(CPACK_NSIS_DEFINES "RequestExecutionLevel admin")
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "ExecWait '$INSTDIR\\\\bin\\\\tuntap-install.exe /S'\\nExecWait '$INSTDIR\\\\bin\\\\lokinet.exe --install'\\nExecWait '$INSTDIR\\\\bin\\\\lokinet.exe -g C:\\\\ProgramData\\\\lokinet\\\\lokinet.ini'\\nCopyFiles '$INSTDIR\\\\share\\\\bootstrap.signed' C:\\\\ProgramData\\\\lokinet\\\\bootstrap.signed")
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "ExecWait '$INSTDIR\\\\bin\\\\lokinet.exe --remove'\\nRMDir /r /REBOOTOK C:\\\\ProgramData\\\\lokinet")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Lokinet.lnk' '$INSTDIR\\\\share\\\\gui\\\\lokinet-gui.exe'"
)
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$SMPROGRAMS\\\\$START_MENU\\\\Lokinet.lnk'"
)
