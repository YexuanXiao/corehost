#include "key_map.hpp"
#include "line_editor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

int main()
{
    using namespace corehost::consoleinput;
    using namespace corehost::vtparser;

    raw_u16buffer raw;
    vt_parser parser{raw};
    auto result = parser.parse(u"\x1bOA");
    auto key = key_from_message(result.id, result.message);
    if (!key.has_value() || key->key != virtual_key::up)
    {
        std::fprintf(stderr, "key mapping failed\n");
        return EXIT_FAILURE;
    }

    line_editor editor;
    editor.toggle_insert();
    editor.append(u"AB");
    editor.consume_completed_line();
    editor.append(u"A");
    editor.left();
    editor.insert(u'C');
    if (editor.text() != u"C")
    {
        std::fprintf(stderr, "insert mode persistence failed\n");
        return EXIT_FAILURE;
    }

    line_editor codepoints;
    codepoints.append(u"e\u0301");
    codepoints.backspace();
    if (codepoints.text() != u"e" || codepoints.cursor() != 1)
    {
        std::fprintf(stderr, "combining mark backspace failed\n");
        return EXIT_FAILURE;
    }
    codepoints.insert(u'\u0301');
    if (codepoints.text() != u"e\u0301")
    {
        std::fprintf(stderr, "combining mark reinsert failed\n");
        return EXIT_FAILURE;
    }

    codepoints.reset();
    codepoints.append(u"\U0001F600");
    codepoints.backspace();
    if (!codepoints.text().empty())
    {
        std::fprintf(stderr, "surrogate pair backspace failed\n");
        return EXIT_FAILURE;
    }

    input_queue queue;
    queue.push(input_event{input_event_type::key, *key});
    queue.push(input_event{input_event_type::key, key_event{}});
    if (queue.record_count() != 2)
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
    input_event metadata_text_event{};
    metadata_text_event.type = input_event_type::text;
    metadata_text_event.text = corehost::vtparser::raw_u16string{u"A"};
    queue.push(std::move(metadata_text_event));
    input_event text_events[2]{};
    if (queue.read(text_events) != 2 || text_events[0].key.character != u'A' ||
        text_events[0].key.key != static_cast<virtual_key>(0) || text_events[0].key.scan_code != 0 ||
        text_events[0].key.control_state != 0 || !text_events[0].key.key_down || text_events[1].key.key_down)
    {
        std::fprintf(stderr, "text metadata failed\n");
        return EXIT_FAILURE;
    }

    queue.clear();
    queue.push(input_event{input_event_type::key, key_event{u'A'}});
    queue.push_front(input_event{input_event_type::key, key_event{u'B'}});
    input_event ordered[2]{};
    if (queue.read(ordered) != 2 || ordered[0].key.character != u'B' || ordered[1].key.character != u'A')
    {
        std::fprintf(stderr, "push_front order failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(u"\x1b[1;5A");
    key = key_from_message(result.id, result.message);
    if (!key.has_value() || key->key != virtual_key::up || key->control_state != 0x0008)
    {
        std::fprintf(stderr, "ctrl key mapping failed\n");
        return EXIT_FAILURE;
    }

    queue.clear();
    input_event text_event{};
    text_event.type = input_event_type::text;
    text_event.text = corehost::vtparser::raw_u16string{u"ABC"};
    queue.push(std::move(text_event));
    if (queue.record_count() != 6 || !queue.has_key())
    {
        std::fprintf(stderr, "text batch accounting failed\n");
        return EXIT_FAILURE;
    }
    input_event expanded[8]{};
    const auto expanded_count = queue.peek(expanded);
    if (expanded_count != 6 || expanded[0].key.character != u'A' || expanded[0].key.key != virtual_key{} ||
        expanded[1].key.key_down)
    {
        std::fprintf(stderr, "text batch expansion metadata failed\n");
        return EXIT_FAILURE;
    }
    if (queue.read(expanded) != 6 || !queue.empty() || queue.has_key())
    {
        std::fprintf(stderr, "text batch read failed\n");
        return EXIT_FAILURE;
    }

    queue.clear();
    text_event.text = corehost::vtparser::raw_u16string{u"AB"};
    queue.push(std::move(text_event));
    input_event one[1]{};
    if (queue.read(one) != 1 || !one[0].key.key_down || one[0].key.character != u'A')
    {
        std::fprintf(stderr, "single slot read failed\n");
        return EXIT_FAILURE;
    }
    if (queue.record_count() != 3)
    {
        std::fprintf(stderr, "pending up accounting failed\n");
        return EXIT_FAILURE;
    }
    input_event pending[4]{};
    if (queue.peek(pending) != 3 || pending[0].key.key_down || pending[0].key.character != u'A')
    {
        std::fprintf(stderr, "pending up peek failed\n");
        return EXIT_FAILURE;
    }

    parser.reset();
    result = parser.parse(u"\x1b[<0;5;3M");
    const auto mouse = mouse_from_message(result.id, result.message);
    if (!mouse.has_value() || mouse->x != 4 || mouse->y != 2 || mouse->button_state != 1)
    {
        std::fprintf(stderr, "mouse mapping failed\n");
        return EXIT_FAILURE;
    }

    std::printf("libconsole input tests passed\n");
    return EXIT_SUCCESS;
}