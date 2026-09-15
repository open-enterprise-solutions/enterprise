// =============================================================================
// The translation constructor — what OK does to a translated text.
//
// One window for a caption in the property grid and for a string literal in a
// module (ibDialogTranslateConstructor). Its failures are all quiet: a language
// dropped, an empty translation written where there was none, the order changed.
// Nothing crashes and the text still reads — just not as it was written. These
// tests pin the rule (Collect, no window needed) and one real trip through the
// boxes.
// =============================================================================

#include "frontendFix.h"

#include "frontend/win/dlgs/translateConstructor/translateConstructor.h"

#include <wx/frame.h>

namespace {

ibBackendLocalizationEntryArray Boxes(std::initializer_list<ibBackendLocalizationEntry> boxes)
{
	return ibBackendLocalizationEntryArray(boxes);
}

} // namespace

// A code the configuration has no language for gets no box of the configuration's — and must survive.
// The grid's old window rebuilt the text from the configuration's languages and lost it.
TEST(TranslateConstructorCollect, ACodeWithNoLanguageIsKeptWhereItWas)
{
	const ibTranslateString original(wxT("en = 'Total'; de = 'Gesamt'; ru = 'Itogo';"));
	const ibTranslateString collected = ibDialogTranslateConstructor::Collect(original,
		Boxes({ { wxT("en"), wxT("Total") }, { wxT("ru"), wxT("Vsego") }, { wxT("uk"), wxT("") } }));

	EXPECT_EQ(wxT("en = 'Total';de = 'Gesamt';ru = 'Vsego';"), collected.GetRawText());
}

// An empty box is "not translated". Written back as `uk = ''` it made Tstr answer nothing for uk
// instead of falling back to another language.
TEST(TranslateConstructorCollect, AnEmptyBoxTakesTheLanguageOut)
{
	const ibTranslateString original(wxT("en = 'Total'; ru = 'Itogo';"));
	const ibTranslateString collected = ibDialogTranslateConstructor::Collect(original,
		Boxes({ { wxT("en"), wxT("Total") }, { wxT("ru"), wxT("") } }));

	EXPECT_EQ(wxT("en = 'Total';"), collected.GetRawText());
	wxString ru;
	EXPECT_FALSE(collected.FindTranslate(wxT("ru"), ru));
}

// Nothing typed, nothing changed — which is what lets the code editor leave a literal in the spelling
// its author gave it.
TEST(TranslateConstructorCollect, UntouchedBoxesGiveTheSameText)
{
	const ibTranslateString original(wxT("en = 'Not enough'; ru = 'Nedostatochno'"));
	const ibTranslateString collected = ibDialogTranslateConstructor::Collect(original,
		Boxes({ { wxT("en"), wxT("Not enough") }, { wxT("ru"), wxT("Nedostatochno") }, { wxT("uk"), wxT("") } }));

	EXPECT_TRUE(collected == original) << collected.GetRawText().ToStdString();
}

// A language's code is matched the way it is read, without regard to case: `EN` in the text and `en`
// in the configuration are one language, not two cells.
TEST(TranslateConstructorCollect, TheCodeIsMatchedWithoutRegardToCase)
{
	const ibTranslateString original(wxT("EN = 'Total';"));
	const ibTranslateString collected = ibDialogTranslateConstructor::Collect(original,
		Boxes({ { wxT("en"), wxT("Sum") } }));

	EXPECT_EQ(1u, collected.GetTranslations().size());
	EXPECT_EQ(wxT("Sum"), collected.FindTranslate(wxT("en")));
}

// ------------------------- through the window -------------------------------

namespace {

class TranslateConstructorFix : public FrontendRuntimeFix
{
protected:
	void SetUp() override
	{
		FrontendRuntimeFix::SetUp();     // GTEST_SKIPs on a headless box
		if (!ready) return;
		m_frame = new wxFrame(nullptr, wxID_ANY, wxT("translate"));
	}

	void TearDown() override
	{
		if (m_frame != nullptr) { m_frame->Destroy(); m_frame = nullptr; }
		FrontendRuntimeFix::TearDown();
	}

	wxFrame* m_frame = nullptr;
};

} // namespace

// Every code the text holds gets a box, filled with exactly its own text, and a window closed without
// an edit hands the text back as it came — the language in force and a code no configuration declares
// alike.
TEST_F(TranslateConstructorFix, TheBoxesHandTheTextBackAsItCame)
{
	if (m_frame == nullptr) GTEST_SKIP();

	const ibTranslateString original(wxT("en = 'Total'; de = 'Gesamt';"));
	ibDialogTranslateConstructor dialog(m_frame, wxT("Translation"), original, nullptr, /*readOnly*/ false);

	const ibTranslateString back = dialog.GetTranslate();
	EXPECT_TRUE(back == original) << back.GetRawText().ToStdString();
	EXPECT_EQ(wxT("Gesamt"), back.FindTranslate(wxT("de")));
}
