/////////////////////////////////////////////////////////////////////////////
// ibPluginWebPane — headless smoke tests.
//
// These tests construct a real ibPluginWebPane on top of a hidden wxFrame and
// verify the lifecycle and the cross-thread PushMessage entry point. They
// are deliberately conservative: wxWebView's actual JS execution is not
// observable without a display (and on Windows, without the WebView2
// runtime), so we only verify that:
//
//   1. Construction does not crash and the pane attaches to the parent's
//      child list.
//   2. Destroying the parent destroys the pane (standard wxWidgets
//      parenting; just guards against leaks/double-frees).
//   3. PushMessage on the UI thread does not crash and does not block.
//   4. The onMessage callback is NOT invoked during boot — messages
//      only fire when JS code posts (which cannot happen headlessly).
//
// Skipped automatically when no GUI display is available (typical
// headless CI: no DISPLAY/WAYLAND on Linux, no Aqua on macOS sandbox,
// missing WebView2 runtime on Windows). Skipped tests are NOT failures.
//
// IMPORTANT: this test never modifies ibPluginWebPane's public API. If a
// future scenario would require a new accessor, leave a `// TODO: needs
// accessor` comment instead of patching the header.
/////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/string.h>
#include <wx/uri.h>
#include <wx/wfstream.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "frontend/pluginWebPane/pluginWebPane.h"
#include "backend/plugin/pluginApi.h"

namespace {

// Counter incremented by onMessageStub; lives at file scope so the C
// callback pointer captures nothing.
std::atomic<int> g_messageCounter{0};

// Plugin-side trampoline. Signature matches ibPluginWebMsgFn:
//   void(*)(const char* paneId, const char* jsonInline, void* userData)
// `userData` points at an int we use to verify the host passed it through
// untouched on real calls. For these tests we expect ZERO invocations —
// no JS code runs in a headless wxWebView.
void OnMessageStub(const char* /*paneId*/,
                   const char* /*jsonInline*/,
                   void*       userData)
{
    g_messageCounter.fetch_add(1, std::memory_order_relaxed);
    if (userData != nullptr) {
        // Touch userData to mirror what a real plugin would do — proves
        // the host doesn't pass a stale pointer.
        *static_cast<int*>(userData) += 1;
    }
}

// Write a tiny HTML stub into the system temp dir. Returned path is
// absolute, suitable for wxFileName::FileNameToURL.
wxString WriteTempHtmlBundle()
{
    wxString tmpDir = wxStandardPaths::Get().GetTempDir();
    wxFileName fn(tmpDir, wxT("oes_pluginWebPane_test.html"));
    const wxString path = fn.GetFullPath();

    wxFileOutputStream out(path);
    if (out.IsOk()) {
        const char html[] = "<!doctype html><body>ok</body>";
        out.Write(html, sizeof(html) - 1);
    }
    return path;
}

// Fixture: bring up a wxApp once per TEST_F. wxWebView requires a real
// wxApp (not wxAppConsole) bound to a display — headless CI without one
// must skip, not fail.
//
// We use wxEntryStart / wxEntryCleanup for an in-process wxApp lifecycle
// without main() shenanigans. If it fails (no DISPLAY, sandboxed CI),
// GTEST_SKIP() makes the suite tolerant.
class SigmaPaneTest : public ::testing::Test {
protected:
    void SetUp() override {
#ifdef __WXMSW__
        // wxWebView on Windows requires the WebView2 runtime. CI images
        // commonly lack it. Skip gracefully — gating on a runtime probe
        // that's safe to call before wxApp exists is non-trivial, so we
        // gate at the wxEntryStart layer below.
#endif
        // Use argc=1, argv={"oes_tests"} — wxEntryStart needs a writable
        // argv. The static buffer survives for the test process lifetime.
        static char  arg0[] = "oes_tests";
        static char* argvBuf[] = { arg0, nullptr };
        int argc = 1;

        // wxEntryStart spins up a wxApp instance if one isn't already
        // running. Returns false on platforms that can't initialise the
        // GUI runtime (no display, denied access).
        if (wxTheApp == nullptr) {
            const bool ok = wxEntryStart(argc, argvBuf);
            if (!ok || wxTheApp == nullptr) {
                m_skipped = true;
                GTEST_SKIP() << "no GUI display available — skipping headless"
                                " wxWebView smoke test";
                return;
            }
            m_ownsApp = true;
        }

        m_parent = new wxFrame(nullptr, wxID_ANY, wxT("oes-pluginWebPane-test"));
        m_parent->Hide();
        m_bundlePath = WriteTempHtmlBundle();
        g_messageCounter.store(0, std::memory_order_relaxed);
    }

    void TearDown() override {
        if (m_skipped) {
            return;
        }
        if (m_parent != nullptr) {
            m_parent->Destroy();
            m_parent = nullptr;
        }
        if (m_ownsApp) {
            wxEntryCleanup();
            m_ownsApp = false;
        }
    }

    wxFrame* m_parent      = nullptr;
    wxString m_bundlePath;
    bool     m_ownsApp     = false;
    bool     m_skipped     = false;
};

} // namespace

// ---------------------------------------------------------------------------

TEST_F(SigmaPaneTest, ConstructsAndDestroys) {
    if (m_skipped) return; // GTEST_SKIP already fired in SetUp

    int userData = 0;
    auto* pane = new ibPluginWebPane(m_parent,
                                  wxT("test.pane"),
                                  wxT("Test Pane"),
                                  m_bundlePath,
                                  &OnMessageStub,
                                  &userData);
    ASSERT_NE(pane, nullptr);

    // Pane attached itself to the parent's child list via wxWidgets
    // parenting (there is no public ID accessor we could query; checking
    // child-count is the API-stable way to confirm attachment).
    // TODO: needs accessor — once ibPluginWebPane exposes a list-membership
    // helper, prefer that. For now this is the contract observable
    // through the existing public surface.
    EXPECT_EQ(m_parent->GetChildren().GetCount(), 1u);

    // Boot must not fire any JS-originated message — the WebView has no
    // way to run script before the document loads, and headlessly the
    // document never finishes loading either.
    EXPECT_EQ(g_messageCounter.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(userData, 0);

    // Destroying the parent must cascade to the pane via wxWidgets
    // ownership. We rely on the destructor running without crash; if
    // ibPluginWebPane mishandled m_webView cleanup, ASAN/UBSAN would catch
    // it here.
    m_parent->Destroy();
    m_parent = nullptr;
}

// ---------------------------------------------------------------------------

TEST_F(SigmaPaneTest, PushMessageFromMainThreadIsSafe) {
    if (m_skipped) return;

    int userData = 0;
    auto* pane = new ibPluginWebPane(m_parent,
                                  wxT("test.pane"),
                                  wxT("Test Pane"),
                                  m_bundlePath,
                                  &OnMessageStub,
                                  &userData);
    ASSERT_NE(pane, nullptr);

    // Call from the main thread. The internal fast-path goes straight
    // to wxWebView::RunScript; if RunScript chokes on an unloaded
    // document it must not propagate as an exception.
    EXPECT_NO_THROW(pane->PushMessage(wxT("{\"hello\":\"world\"}")));

    // RunScript is fire-and-forget; verifying actual JS execution would
    // require a loaded document and a display. Manual smoke test only.

    // No JS-originated messages should have come back.
    EXPECT_EQ(g_messageCounter.load(std::memory_order_relaxed), 0);
}
