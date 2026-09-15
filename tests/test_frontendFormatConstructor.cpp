// =============================================================================
// The format string constructor — one real trip through the tabs.
//
// The rules live in the backend (test_formatString); what can go wrong HERE is
// a row that does not carry its code back: a box read as unticked, a separator
// that is not among the usual ones, a code no tab has a row for. A window
// closed without an edit must hand the string back as it came.
// =============================================================================

#include "frontendFix.h"

#include "frontend/win/dlgs/formatConstructor/formatConstructor.h"

#include <wx/frame.h>

namespace {

class FormatConstructorFix : public FrontendRuntimeFix
{
protected:
	void SetUp() override
	{
		FrontendRuntimeFix::SetUp();     // GTEST_SKIPs on a headless box
		if (!ready) return;
		m_frame = new wxFrame(nullptr, wxID_ANY, wxT("format"));
	}

	void TearDown() override
	{
		if (m_frame != nullptr) { m_frame->Destroy(); m_frame = nullptr; }
		FrontendRuntimeFix::TearDown();
	}

	wxFrame* m_frame = nullptr;
};

} // namespace

TEST_F(FormatConstructorFix, TheTabsHandTheStringBackAsItCame)
{
	if (m_frame == nullptr) GTEST_SKIP();

	const wxString texts[] = {
		wxT("NFD=2; NGS= ; NZ=-"),
		wxT("ND=10; NDS=,; NG=4; NZ="),
		wxT("DF=HH:MM; DE=-"),
		wxT("BT=Yes; BF=No"),
		wxT("NGS=_; NLZ=1"),   // a separator none of the usual ones, and a code no tab has
	};
	for (const wxString& text : texts) {
		const ibFormatString original = ibFormatString::Parse(text);
		ibDialogFormatConstructor dialog(m_frame, wxT("Format"), original, /*readOnly*/ false);
		const ibFormatString back = dialog.GetFormat();
		EXPECT_TRUE(back == original) << text.ToStdString() << " came back as " << back.Render().ToStdString();
	}
}
