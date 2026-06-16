# Third-party notices

copse itself contains no third-party code and has no runtime dependencies: the
distributed library and headers are original work under the MIT License (see
`LICENSE`), and the installed package links nothing external.

The components below are used only at build, test, benchmark, or demo time and
are **not** part of the distributed library or its installed artifacts:

| Component | License | Used for | Vendored? |
| --- | --- | --- | --- |
| [Catch2](https://github.com/catchorg/Catch2) | BSL-1.0 | unit tests and Catch2 microbenchmarks | no (FetchContent) |
| [HKU MARS ikd-Tree](https://github.com/hku-mars/ikd-Tree) | GPL-2.0 | comparison baseline for the `bench_perf_ikd` benchmark only | no (FetchContent, never distributed) |
| [Eigen](https://eigen.tuxfamily.org) | MPL-2.0 | the ikd comparison harness only | no (system / `find_package`) |
| [Polyscope](https://github.com/nmwsharp/polyscope) | MIT | optional 3D demos (`COPSE_BUILD_DEMOS`, off by default) | no (FetchContent) |

The `bench_perf_ikd` benchmark links GPL-2.0 ikd-Tree, so that benchmark binary
is a GPL-2.0 derivative. It is an internal benchmark only and is never built into
or distributed with the library.
