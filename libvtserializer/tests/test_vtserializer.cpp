#include "buffer.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    using namespace corehost::vtserializer;

    byte_buffer buffer;
    buffer.reserve(4);
    buffer.append_text(u8"AB");
    buffer.push_back(static_cast<char8_t>(u8'C'));
    if (buffer.size() != 3 || buffer.bytes().size() != 3 || buffer.bytes()[2] != u8'C')
    {
        std::fprintf(stderr, "append failed\n");
        return EXIT_FAILURE;
    }

    buffer.clear();
    if (!buffer.empty())
    {
        std::fprintf(stderr, "clear failed\n");
        return EXIT_FAILURE;
    }
    buffer.append_text(u8"xy");
    if (buffer.bytes().size() != 2 || buffer.bytes()[0] != u8'x')
    {
        std::fprintf(stderr, "reuse failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libvtserializer tests passed\n");
    return EXIT_SUCCESS;
}