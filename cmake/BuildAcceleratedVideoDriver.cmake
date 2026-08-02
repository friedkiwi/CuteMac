file(READ "${BASE_SOURCE}" source)
string(REPLACE "beq.w   install_guest_helpers" "beq.w   accelerated_install_guest_helpers" source "${source}")
file(READ "${EXTENSION_SOURCE}" extension)
file(WRITE "${OUTPUT_SOURCE}" "${source}\n${extension}\n")
