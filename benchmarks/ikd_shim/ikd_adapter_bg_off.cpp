// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// Mode (b) adapter — ikd background rebuild OFF. The rebuild-gate and rename
// macros come from `-include ikd_bg_off.h` on this object target; the shared
// body binds them to the facade.
#include "ikd_adapter.inc"

namespace ikd_facade {

Tree* make_bg_off(float delete_param, float balance_param, float box_length) {
    return new IkdTree(delete_param, balance_param, box_length);
}

} // namespace ikd_facade
