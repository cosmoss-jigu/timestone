#include <timestone++.hpp>

using namespace ts;

thread_local ::ts_thread_struct_t *ts_thread::self;
thread_local std::jmp_buf ts_thread::bots;
