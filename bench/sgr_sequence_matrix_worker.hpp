#pragma once

// Worker side for sgr-sequence-matrix. Each run emits one selected SGR case so
// the parent can compare per-sequence throughput.

#include "sgr_sequence_matrix_cases.hpp"
#include "terminal_output_worker.hpp"

namespace bench
{

// --emit-sgr-case <case-name> <target-bytes> [marker] [ready-marker] [trigger-event]
inline int emit_sgr_sequence_case_to_stdout(const wchar_t *case_name, size_t target_bytes, const wchar_t *marker,
                                            const wchar_t *ready_marker, const wchar_t *trigger_event_name)
{
    const auto name = narrow(case_name);
    const auto *test_case = find_sgr_sequence_case(name);
    if (!test_case)
        print_and_abort("unknown SGR case: %s\n", name.c_str());

    const auto line = make_sgr_sequence_line(*test_case);
    return emit_repeated_output_line(line, target_bytes, marker, ready_marker, trigger_event_name);
}

} // namespace bench
