# KDTree

Fixed-capacity, mutable kd-tree for low-dimensional point data. The tree
maintains a FIFO buffer of points, silently overwriting the oldest when full,
and enforces a minimum-spacing (`resolution`) invariant that rejects
near-duplicate inserts.

## References
- `docs/design/fixed-size-kdtree.md` — design plan, API surface, tuning,
  trade-offs.
- `docs/research/fixed_size_kdtree.md` — informational research background.
