#ifndef __TRANSLATE_CONSTRUCTOR_DLG_H__
#define __TRANSLATE_CONSTRUCTOR_DLG_H__

////////////////////////////////////////////////////////////////////////////
// The TRANSLATION CONSTRUCTOR — one box per language over a translated text.
////////////////////////////////////////////////////////////////////////////
//
// ONE WINDOW, TWO WAYS IN. A caption in the property grid (wxTStringProperty) and a string literal
// in a module (the code editor's menu, beside the query constructor) hold the same thing - an
// ibTranslateString, written `en = 'Total'; ru = '...';` - so both open this window over it. Two
// windows would be two rules about what OK does to a text, and the grid's own already had three
// quiet ones, all fixed here:
//
//   * A CODE THE CONFIGURATION HAS NO LANGUAGE FOR WAS DROPPED. The text was rebuilt from the
//     configuration's languages alone, so a `de = '...'` written by hand, or brought in with a
//     module, was gone after the first OK. Such codes get boxes of their own, apart from the
//     configuration's, and are kept.
//   * AN EMPTY BOX WAS WRITTEN AS AN EMPTY TRANSLATION. A language the text has no cell for opens
//     empty; writing `uk = ''` back made Tstr answer nothing for uk instead of falling back to
//     another language - a message that vanished because a window was opened and closed. An empty
//     box means "not translated", and its cell is taken out.
//   * THE ORDER WAS THE ORDER OF POINTERS in a std::map. The first language is the one a reader
//     falls back to last, so the text that came in is edited in place and keeps its order.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"
#include "backend/backend_localization.h"   // ibTranslateString — what the boxes are over

#include <wx/dialog.h>
#include <wx/textctrl.h>

#include <functional>
#include <utility>
#include <vector>

class ibMetaData;

class FRONTEND_API ibDialogTranslateConstructor : public wxDialog
{
public:

	// WHAT EDITS ONE BOX THROUGH A WINDOW OF ITS OWN. Given, every box gets a `...` beside it that hands
	// the box's text over and, when the call says yes, takes it back — the format property opens the
	// format string constructor there (advpropString.cpp). `language` is what the box is labelled with.
	// This window knows nothing about what the text is.
	using ibBoxEditor = std::function<bool(wxWindow* parent, const wxString& language, wxString& text)>;

	// `metaData` names the languages (an extension's are its owner's); without one there is a single
	// box, for the language in force, beside whatever codes the text already holds.
	ibDialogTranslateConstructor(wxWindow* parent, const wxString& title, const ibTranslateString& text,
		const ibMetaData* metaData, bool readOnly, int maxLength = 0, const ibBoxEditor& boxEditor = ibBoxEditor());

	// What the boxes say, laid over the text that came in (Collect).
	ibTranslateString GetTranslate() const;

	// THE RULE OK APPLIES, on its own so it can be read — and tested — without a window: every box
	// writes its language into the original text, an empty box takes that language out, and whatever
	// had no box stays as it was, where it was.
	static ibTranslateString Collect(const ibTranslateString& original,
		const ibBackendLocalizationEntryArray& boxes);

private:

	ibTranslateString m_original;

	// Each box with the code it writes, in the order they stand in the window.
	std::vector<std::pair<wxString, wxTextCtrl*>> m_boxes;
};

#endif
