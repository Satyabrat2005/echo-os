# echo-common: shared vocabulary and utilities used by every runtime module.
add_library(echo-common STATIC
    src/types.cpp
    src/result.cpp
    src/latency.cpp
    src/log.cpp
    src/config.cpp
    # Phase 22 moved three primitives DOWN into common so that two modules on
    # opposite sides of the privacy boundary could share one implementation each
    # rather than grow a second. json.cpp came from apps/appkit (apps/ builds
    # after, and optionally without, the core); crypto/aes256.cpp came from
    # memory/ (companion-sync must never link the store). crypto/cmac.cpp is new:
    # the inbound caregiver path is the first place this system needs integrity,
    # not just confidentiality. See ADR-19.
    src/json.cpp
    src/consent.cpp
    src/crypto/aes256.cpp
    src/crypto/cmac.cpp
    src/crypto/key_store.cpp
    src/crypto/sealed_box.cpp      # encrypt-then-MAC composition (Phase 24)
    src/crypto/install_binding.cpp # per-install key binding, opt-in (Phase 24)
)

target_include_directories(echo-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Every module links this, so give it the project-wide warning/standard settings.
target_link_libraries(echo-common PUBLIC echo-project-options)

# install_binding.cpp reads HKLM\...\MachineGuid via RegGetValueA (advapi32) on Windows.
# Read-only registry access; no other module has needed the Windows registry before this.
if(WIN32)
    target_link_libraries(echo-common PRIVATE advapi32)
endif()

add_library(echo::common ALIAS echo-common)
