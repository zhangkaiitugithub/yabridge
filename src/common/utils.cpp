// yabridge: a Wine VST bridge
// Copyright (C) 2020-2021 Robbert van der Helm
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "utils.h"

#include <sched.h>
#include <xmmintrin.h>
#include <boost/process/environment.hpp>

namespace bp = boost::process;
namespace fs = boost::filesystem;

fs::path get_temporary_directory() {
    bp::environment env = boost::this_process::environment();
    if (!env["XDG_RUNTIME_DIR"].empty()) {
        return env["XDG_RUNTIME_DIR"].to_string();
    } else {
        return fs::temp_directory_path();
    }
}

std::optional<int> get_realtime_priority() {
    sched_param current_params{};
    if (sched_getparam(0, &current_params) == 0 &&
        current_params.sched_priority > 0) {
        return current_params.sched_priority;
    } else {
        return std::nullopt;
    }
}

bool set_realtime_priority(bool sched_fifo, int priority) {
    sched_param params{.sched_priority = (sched_fifo ? priority : 0)};
    return sched_setscheduler(0, sched_fifo ? SCHED_FIFO : SCHED_OTHER,
                              &params) == 0;
}

ScopedFlushToZero::ScopedFlushToZero() {
    old_ftz_mode = _MM_GET_FLUSH_ZERO_MODE();
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
}

ScopedFlushToZero::~ScopedFlushToZero() {
    if (old_ftz_mode) {
        _MM_SET_FLUSH_ZERO_MODE(*old_ftz_mode);
    }
}

ScopedFlushToZero::ScopedFlushToZero(ScopedFlushToZero&& o)
    : old_ftz_mode(std::move(o.old_ftz_mode)) {
    o.old_ftz_mode.reset();
}

ScopedFlushToZero& ScopedFlushToZero::operator=(ScopedFlushToZero&& o) {
    old_ftz_mode = std::move(o.old_ftz_mode);
    o.old_ftz_mode.reset();

    return *this;
}
