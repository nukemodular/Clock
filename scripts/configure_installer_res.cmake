configure_file(
    "${SRC_DIR}/installer_resources/distribution.xml.in"
    "${BIN_DIR}/installer_resources/distribution.xml"
    @ONLY
)
configure_file(
    "${SRC_DIR}/installer_resources/welcome.html.in"
    "${BIN_DIR}/installer_resources/welcome.html"
    @ONLY
)

