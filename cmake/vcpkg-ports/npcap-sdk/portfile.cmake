# Build-time half of Windows bridged networking. The SDK supplies pcap headers
# and the wpcap import library; the runtime half is the user's own Npcap
# installation. Npcap itself may not be redistributed without a licence from the
# Nmap Project, so nothing here installs a DLL and CuteMac delay-loads
# wpcap.dll instead of shipping one.
#
# The upstream vcpkg libpcap port is not an option on Windows: without
# Packet_ROOT it configures with -DPCAP_TYPE=null, which enumerates zero
# interfaces and fails every capture attempt.

set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_POLICY_SKIP_DUMPBIN_CHECKS enabled)

vcpkg_download_distfile(ARCHIVE
    URLS "https://npcap.com/dist/npcap-sdk-${VERSION}.zip"
    FILENAME "npcap-sdk-${VERSION}.zip"
    SHA512 19ab8ef0fad0c0385541694ff281c960cb889e7e6d0904784d12eafe5e319dd7d29d9b947231432a63b4e63c73a9ea2a8ea7833bbe4ba2513e28f9d77ecd206c
)

# The archive has no top-level directory: Include/, Lib/ and docs/ sit at its root.
vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
    NO_REMOVE_ONE_LEVEL
)

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(NPCAP_LIB_DIR "${SOURCE_PATH}/Lib/x64")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(NPCAP_LIB_DIR "${SOURCE_PATH}/Lib/ARM64")
elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(NPCAP_LIB_DIR "${SOURCE_PATH}/Lib")
else()
    message(FATAL_ERROR "The Npcap SDK ships no import libraries for ${VCPKG_TARGET_ARCHITECTURE}.")
endif()

file(INSTALL "${SOURCE_PATH}/Include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL
        "${NPCAP_LIB_DIR}/wpcap.lib"
        "${NPCAP_LIB_DIR}/Packet.lib"
    DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL
        "${NPCAP_LIB_DIR}/wpcap.lib"
        "${NPCAP_LIB_DIR}/Packet.lib"
    DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")

# The SDK archive carries no licence file of its own.
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright"
    "Npcap is copyright (c) the Nmap Project. It is not open source and may not be\n"
    "redistributed without permission; see https://npcap.com/#license. This port\n"
    "installs only the SDK's headers and import libraries for building against a\n"
    "separately installed Npcap runtime.\n")
