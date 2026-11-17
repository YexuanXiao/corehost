add_library(defterm2 STATIC
    defterm.cpp
)

target_sources(defterm2
    PRIVATE
        FILE_SET HEADERS
            FILES
                console_session.hpp
                defterm.hpp
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
    user32
)

target_compile_features(defterm2 PRIVATE cxx_std_23)
