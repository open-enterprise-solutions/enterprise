# Unreleased

The engine and wxWidgets build as C++20, MSVC under strict conformance ([portability.md § 1.11](../portability.md)). On the `.sln` road the prebuilt wxWidgets binaries still link — MSVC's ABI does not change with the standard — and are rebuilt as C++20 by copying `build\msw\cxx20.props` to `wx_local.props` ([BUILD.md](../BUILD.md#wxwidgets-submodule-details)).

wxWidgets moves from 3.3.2 to 3.3.4: a crash in the Edge backend of wxWebView fixed, Esc that IME or auto-completion used no longer closes the dialog (Windows), wxGrid crashes on changing the editor fixed, dark-mode fixes, wx's third-party libraries (png, expat, tiff, pcre) updated. Under GTK and macOS the module editor (wxStyledTextCtrl) now composes IME input inline.

An old binary on the same base still acquires `sys_lock` without the `sys_lock_key` header row. Until every process is upgraded, two processes — one of them not upgraded — can both be granted the same key.
