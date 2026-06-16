// Mode (c) — ikd-Tree as shipped, background rebuild ON.
//
// Force-included (`-include`) ahead of the upstream `ikd_Tree.h` when compiling
// `ikd_Tree.cpp` and the BG-on adapter for the BG-on object target.
//
//   * `Multi_Thread_Rebuild_Point_Num = 1500` is the upstream default, so
//     subtrees of >= 1500 nodes are handed to the background rebuild thread
//     (the FAST-LIO2 configuration). Defined here rather than inherited so both
//     modes select the value through the same explicit mechanism; the
//     configure-time patch deletes the upstream `#define`, so this is the sole
//     definition.
//   * `KD_TREE` is renamed per mode so the BG-on and BG-off object targets emit
//     distinct symbols and link into one executable without ODR collision. The
//     nested `KD_TREE_NODE` token is untouched (macro replacement is per-token).
#pragma once

#define Multi_Thread_Rebuild_Point_Num 1500
#define KD_TREE KD_TREE_BG_ON
