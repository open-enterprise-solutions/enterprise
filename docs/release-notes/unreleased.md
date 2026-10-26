# Unreleased

The engine and wxWidgets build as C++20, MSVC under strict conformance ([portability.md § 1.11](../portability.md)). On the `.sln` road the prebuilt wxWidgets binaries still link — MSVC's ABI does not change with the standard — and are rebuilt as C++20 by copying `build\msw\cxx20.props` to `wx_local.props` ([BUILD.md](../BUILD.md#wxwidgets-submodule-details)).

An old binary on the same base still acquires `sys_lock` without the `sys_lock_key` header row. Until every process is upgraded, two processes — one of them not upgraded — can both be granted the same key.
