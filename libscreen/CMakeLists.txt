# libscreen — 16-byte cell screen buffer reference library.
add_library(libscreen STATIC
    hyperlink.cpp
    screen_buffer.cpp
    style.cpp
)

target_sources(libscreen
    PUBLIC
        FILE_SET HEADERS
            FILES
                cell.hpp
                grapheme_store.hpp
                hyperlink.hpp
                screen_buffer.hpp
                style.hpp
)

target_include_directories(libscreen PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(libscreen PUBLIC cxx_std_23)
