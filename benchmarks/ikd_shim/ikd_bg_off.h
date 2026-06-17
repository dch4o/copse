// SPDX-FileCopyrightText: 2026 Dohoon Cho
// SPDX-License-Identifier: MIT
// Mode (b) — ikd-Tree single-thread, background rebuild OFF.
//
// Force-included (`-include`) ahead of the upstream `ikd_Tree.h` when compiling
// `ikd_Tree.cpp` and the BG-off adapter for the BG-off object target.
//
//   * `Multi_Thread_Rebuild_Point_Num = INT_MAX` forces every rebuild down the
//     synchronous in-place branch. The configure-time patch deletes the
//     upstream `#define`, so this is the sole definition. The constructor's idle
//     rebuild pthread still spawns (decision: b-idle); only the off-thread
//     rebuild path is disabled.
//   * `KD_TREE` is renamed per mode so the BG-off and BG-on object targets emit
//     distinct symbols and link into one executable without ODR collision. The
//     nested `KD_TREE_NODE` token is untouched (macro replacement is per-token).
#pragma once

#define Multi_Thread_Rebuild_Point_Num 2147483647
#define KD_TREE KD_TREE_BG_OFF
