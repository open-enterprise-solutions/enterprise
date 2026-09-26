#ifndef __CHOICE_LINK_DLG_H__
#define __CHOICE_LINK_DLG_H__

// The visual editor for ibChoiceTypeLinkDescription — WHICH LIST OPENS for this field. Its companion
// beside it (win/dlgs/choiceParameters) answers what is shown IN that list; the two are separate
// windows because they produce different things (docs/private/choice-links.md § 1).
//
// ⭐ A WINDOW RATHER THAN A DROP-DOWN ROW, AND THE ATTRIBUTES WITH THEIR PICTURES. The link used to be
// a property row that unfolded into a second one, and a row can show a name and nothing else: an author
// choosing the field that decides this one's TYPE wants to see WHAT each candidate is before picking
// (Max, 2026-09-23: "a link by type should open a window with the attributes — pictures").
//
// On the house grid, like its companion: ibDataViewCtrl over the constructor's own ibQueryGridModel, so
// a field is drawn here exactly as it is drawn in the tree three inches away.
//
// It edits a BUFFER and writes back only on OK, like every editor of a description here.

#include <wx/dialog.h>
#include <wx/choice.h>
#include <wx/stattext.h>

#include "frontend/win/ctrls/dataview/dataview.h"
#include "backend/choiceLinkDescription.h"

class BACKEND_API ibPropertyChoiceLink;

class ibDialogChoiceLink : public wxDialog {
public:

	ibDialogChoiceLink(wxWindow* parent, ibPropertyChoiceLink* property,
		const ibChoiceTypeLinkDescription& link);

	const ibChoiceTypeLinkDescription& GetLink() const { return m_link; }

	// The designer entry — hands back whether the link changed. Mirrors its companion's.
	static bool ShowChoiceLinkDialog(ibPropertyChoiceLink* property, ibChoiceTypeLinkDescription& link);

private:

	// ONE CANDIDATE — the field, as it is called and as it is drawn. Id 0 is "nothing governs it",
	// which is a choice like any other and so stands in the same list rather than beside it as a button.
	struct ibCandidate {
		ibMetaID m_id = 0;
		wxString m_label;
		wxIcon   m_icon;
	};

	void BuildControls();
	void BuildCatalogue();
	void ShowSelected();

	ibPropertyChoiceLink*       m_property = nullptr;
	ibChoiceTypeLinkDescription m_link;        // the buffer

	std::vector<ibCandidate> m_fields;   // what may govern this one — the property's own answer
	std::vector<ibCandidate> m_types;    // …and which of THIS field's types a link may decide

	// THE PICTURE AN ORDINARY FIELD CARRIES. A field with none drawn as none came up as an EMPTY ROW —
	// the grid's icon-text cell cannot read a plain string. See the note in the .cpp.
	wxIcon m_fieldIcon;

	ibDataViewCtrl*         m_list  = nullptr;
	class ibQueryGridModel* m_model = nullptr;
	wxStaticText*   m_says          = nullptr;   // what the field standing selected would decide
	wxChoice*       m_governed      = nullptr;   // shown only where the field holds more than one type
	wxStaticText*   m_governedLabel = nullptr;
};

#endif // __CHOICE_LINK_DLG_H__
