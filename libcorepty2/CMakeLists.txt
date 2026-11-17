add_library(libcorepty2 INTERFACE)

target_include_directories(libcorepty2 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_parties/deque
)

target_link_libraries(libcorepty2 INTERFACE libvtparser libvtstate libconsole libscreen libvtserializer libpseudowindow libconsoleapi libconsoleinput libcoreio common unicode::unicode user32)

target_compile_features(libcorepty2 INTERFACE cxx_std_23)
