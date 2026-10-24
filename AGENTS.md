# Project instructions

- This is a proprietary, offline-first Windows x64 industrial simulation MVP.
- Keep the functional core independent of Qt, VTK, file I/O, networking and wall-clock timing.
- Preserve deterministic scan semantics and explicit event sequencing.
- Do not introduce educational features.
- MSVC/C++20/CMake/vcpkg is the product toolchain. Alternative compiler results must be labeled separately.
- Build and run GoogleTest/CTest for behavioral changes; retain the golden trace and replay checks.
- Read docs/technical-contract.md and docs/architecture.md before changing cache or simulation semantics.
- Do not mark reserved persistence or desktop dependencies as implemented modules.
- Publish or upload source only when explicitly requested by the user. Do not create external messages without user authorization.
