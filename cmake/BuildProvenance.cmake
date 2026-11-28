# SPDX-License-Identifier: GPL-3.0-or-later

function(swiftify_record_provenance)
    set(lockfile "${SWIFTIFY_CORE_DIR}/Cargo.lock")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${lockfile}")
    file(SHA256 "${lockfile}" lock_hash)
    list(JOIN SWIFTIFY_CORE_FEATURES "," rust_features)
    set(rust_target "${Rust_CARGO_TARGET}")
    if(NOT rust_target)
        set(rust_target "${Rust_CARGO_TARGET_CACHED}")
    endif()
    get_target_property(cargo Rust::Cargo IMPORTED_LOCATION)
    execute_process(
        COMMAND "${cargo}" tree --locked --manifest-path "${SWIFTIFY_CORE_DIR}/Cargo.toml"
            --target "${rust_target}" --no-default-features --features "${rust_features}"
            --edges normal --prefix none --format "{p} | {r}"
        RESULT_VARIABLE cargo_result
        OUTPUT_VARIABLE crate_tree
        ERROR_VARIABLE cargo_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT cargo_result EQUAL 0)
        message(FATAL_ERROR "Could not record Rust dependencies (${cargo_result}): ${cargo_error}")
    endif()
    string(REPLACE "\n" ";" crates "${crate_tree}")
    list(TRANSFORM crates REPLACE " \\(\\*\\)$" "")
    list(TRANSFORM crates REPLACE " \\([^)]*\\) \\|" " |")
    list(REMOVE_DUPLICATES crates)
    list(SORT crates)
    list(JOIN crates "\n" crates)
    set(qt_source "https://download.qt.io/archive/qt/${Qt6_VERSION_MAJOR}.${Qt6_VERSION_MINOR}/${Qt6_VERSION}/single/")
    if(SWIFTIFY_BUNDLED_QT)
        set(keychain_source "https://github.com/frankosterfeld/qtkeychain/tree/${SWIFTIFY_QTKEYCHAIN_REVISION}")
        set(qcoro_source "https://github.com/qcoro/qcoro/tree/${SWIFTIFY_QCORO_REVISION}")
    else()
        set(keychain_source "system package")
        set(qcoro_source "system package")
    endif()
    if(ENABLE_SPOTIFY)
        set(vendor "librespot-core vendor base | https://github.com/librespot-org/librespot/tree/d36f9f1907e8cc9d68a93f8ebc6b627b1bf7267d/core\n")
    else()
        set(keychain_source "disabled")
    endif()
    set(manifest "${SWIFTIFY_LICENSE_DIRECTORY}/build-provenance.txt")
    file(WRITE "${manifest}" "Swiftify ${PROJECT_VERSION}
CMake target ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}
C++ compiler ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}
Spotify ${ENABLE_SPOTIFY}
Soloist ${SWIFTIFY_SOLOIST}
YouTube ${SWIFTIFY_YOUTUBE_CLIENT}
Qt ${Qt6_VERSION} | ${qt_source}
ALSA ${ALSA_VERSION_STRING} | system package
PulseAudio ${PULSE_VERSION} | system package
OpenSSL ${OPENSSL_VERSION} | system package when linked
QtKeychain ${Qt6Keychain_VERSION} | ${keychain_source}
QCoro ${QCoro6_VERSION} | ${qcoro_source}
Corrosion | https://github.com/corrosion-rs/corrosion/tree/${SWIFTIFY_CORROSION_REVISION}
Rust target ${rust_target}
Rust features ${rust_features}
Cargo.lock SHA-256 ${lock_hash}
${vendor}Rust runtime graph (direct and transitive)
${crates}
")
    set(SWIFTIFY_BUILD_PROVENANCE "${manifest}" PARENT_SCOPE)
endfunction()
