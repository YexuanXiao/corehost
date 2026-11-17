add_library(defterm2 STATIC
    defterm.cpp
    signal.cpp
)

target_sources(defterm2
    PRIVATE
        FILE_SET HEADERS
            FILES
                connect_policy.hpp
                console_session.hpp
                defterm.hpp
                handoff_session.hpp
                signal.hpp
                terminal_handoff.hpp
)

target_include_directories(defterm2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(defterm2 PRIVATE
    libconsole
    libconsoleapi
    libconsoleinput
    libcoreio
    libinterop
    libpseudowindow
    libscreen
    libvtparser
    libvtserializer
    libvtstate
    conhost_proxy_headers
    ole32
    user32
)

target_compile_features(defterm2 PRIVATE cxx_std_23)
