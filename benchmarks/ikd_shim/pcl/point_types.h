// PCL-free shim for ikd-Tree.
//
// Upstream `ikd_Tree.h` does `#include <pcl/point_types.h>` solely to pull in
// `Eigen::aligned_allocator` (used by `KD_TREE::PointVector`). PCL is absent in
// this container and unneeded by the bare x/y/z core, so this shim sits ahead
// of the system include path and satisfies that include with Eigen alone. The
// upstream header stays byte-for-byte; only its include resolves here.
#pragma once

#include <Eigen/Core>
