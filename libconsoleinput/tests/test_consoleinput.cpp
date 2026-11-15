#include "key_map.hpp"

#include <cstdio>
#include <cstdlib>

int main()
{
    using namespace corehost::consoleinput;
    using namespace corehost::vtparser;

    raw_u32_buffer raw;
    vt_parser parser{raw};
    auto result = parser.parse(U"\x1bOA");
    auto key = key_from_message(result.id, result.message);
    if (!key.has_value() || key->key != virtual_key::up)
    {
        std::fprintf(stderr, "key mapping failed\n");
        return EXIT_FAILURE;
    }

    input_queue queue;
    queue.push(input_event{input_event_type::key, *key});
    queue.push(input_event{input_event_type::key, key_event{}});
    if (queue.size() != 2)
    {
        std::fprintf(stderr, "queue size failed\n");
        return EXIT_FAILURE;
    }

    input_event events[2]{};
    const auto count = queue.read(events);
    if (count != 2 || !queue.empty() || events[0].key.key != virtual_key::up)
    {
        std::fprintf(stderr, "batch read failed\n");
        return EXIT_FAILURE;
    }

    queue.clear();
    queue.push(input_event{input_event_type::key, key_event{u'A'}});
    queue.push_front(input_event{input_event_type::key, key_event{u'B'}});
    input_event ordered[2]{};
    if (queue.read(ordered) != 2 || ordered[0].key.character != u'B' ||
        ordered[1].key.character != u'A')
    {
        std::fprintf(stderr, "push_front order failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libconsoleinput tests passed\n");
    return EXIT_SUCCESS;
}