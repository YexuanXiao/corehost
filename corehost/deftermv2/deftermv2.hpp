#pragma once

#include <cstdint>
#include "miniio/io_thread.hpp"
#include "win32/handle.hpp"

namespace deftermv2
{

struct deftermv2_result
{
    win32::handle server;
    win32::handle event;
    win32::handle condrv_input;
    win32::handle condrv_output;
    win32::handle vt_in;
    win32::handle vt_out;
    win32::handle vt_in_keepalive;
    win32::handle signal;
    miniio::io_msg initial_connect;
    bool has_initial_connect = false;
    short width = 0;
    short height = 0;
};

deftermv2_result deftermv2_entry(std::uintptr_t condrv_handle);
}
