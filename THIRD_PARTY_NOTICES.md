# Third-party notices

copse itself contains no third-party code and has no runtime dependencies: the
distributed library and headers are original work under the MIT License (see
`LICENSE`), and the installed package links nothing external.

The components below are used only at build, test, benchmark, or demo time and
are **not** part of the distributed library or its installed artifacts:

| Component | License | Used for | Vendored? |
| --- | --- | --- | --- |
| [Catch2](https://github.com/catchorg/Catch2) | BSL-1.0 | unit tests and Catch2 microbenchmarks | no (FetchContent) |
| [Polyscope](https://github.com/nmwsharp/polyscope) | MIT | optional 3D demos (`COPSE_BUILD_DEMOS`, off by default) | no (FetchContent) |
