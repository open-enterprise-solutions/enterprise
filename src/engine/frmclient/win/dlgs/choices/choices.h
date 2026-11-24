#ifndef __CHOICES_DLG_H__
#define __CHOICES_DLG_H__

// THE ITEMS A SWITCH OFFERS — the window of its Choices (the server's ibPropertyChoices): a row an item, the value it
// writes into the field and the text it shows, in the order the switch shows them (docs/private/form-field.md § 4.6).
//
// It edits a BUFFER, not the property: the items are copied in when the window opens and written back only on OK, so
// Cancel really cancels — the shape of the choice parameters' window (choiceParameters.h).
//
// ⭐ A VALUE OF A FINITE TYPE IS PICKED, NOT TYPED. A Boolean's two values, an enumeration's members are the server's —
// the row brings them (its Values: each the Value a row stores, and its Presentation), offered in the cell, and Fill
// lays them all in at once. A value of a type that is typed — a string, a number, a date — is edited in the type's
// own cell (the row's Type), a report parameter's Value (ibRowValueCellRenderer): masked and shaped by the type's
// qualifiers, shown as the type writes it.
//
// ⭐ AN EMPTY PRESENTATION IS THE VALUE'S OWN — what the switch shows then (ibFieldViewSwitch::OnUpdate). The text
// typed is the session's language's, laid into the item's translations; the others stay.

#include "deskgui/univ/dialog.h"

#include <vector>

#include "frmclient/frmclient.h"   // FRMCLIENT_API
#include "core/description/choicesDescription.h"
#include "core/description/typeDescription.h"

class FRMCLIENT_API ibBackendProperty;
class ibDataViewCtrl;
class ibButton;

class ibDialogChoices : public ibDialog {
public:

	ibDialogChoices(wxWindow* parent, ibBackendProperty* property, const ibChoicesDescription& choices);

	// The edited items — read after ShowModal() returns wxID_OK.
	const ibChoicesDescription& GetChoices() const { return m_choices; }

	// The designer's entry: the window over a Choices property, and whether its items changed. Mirrors
	// ibDialogChoiceParameters::ShowChoiceParametersDialog.
	static bool ShowChoicesDialog(ibBackendProperty* property, ibChoicesDescription& choices);

	// ⭐ OK GOES THROUGH OR IT REFUSES, on a row with no value — see the .cpp.
	virtual bool Validate() override;

private:

	// A VALUE THE TYPE HAS — as the row brought it: the node a row stores, and what it reads as.
	struct ibOffered {
		ibDataNode m_value;
		wxString   m_label;
	};

	// What a row's cell says, and a text written into one — the grid's reader and writer.
	wxString RowCellText(unsigned int row, unsigned int col) const;
	bool SetRowCellText(unsigned int row, unsigned int col, const wxString& text);

	// What a value reads as — its offered label, or its text as the field's type writes it.
	wxString ValueText(const ibDataNode& value) const;
	// The value a picked text names — an offered one, by its label.
	bool ReadValue(const wxString& text, ibDataNode& value) const;

	void FillRows();
	long SelectedLine() const;
	void SelectLine(long line);
	void ShowSelection();

	void OnAdd(wxCommandEvent&);
	void OnRemove(wxCommandEvent&);
	void OnMoveUp(wxCommandEvent&);
	void OnMoveDown(wxCommandEvent&);
	void OnFill(wxCommandEvent&);

	ibChoicesDescription   m_choices;    // the buffer
	ibTypeDescription      m_type;       // the field's — what a typed value is read as
	const class ibMetaData* m_metaData = nullptr;   // the field's application — the one its type is read against
	std::vector<ibOffered> m_offered;    // the values of its finite classes, in the server's order
	wxArrayString          m_offeredWords;

	ibDataViewCtrl*         m_rows = nullptr;
	class ibQueryGridModel* m_rowsModel = nullptr;
	ibButton*               m_fill = nullptr;   // shown where there is something to lay in
	ibButton*               m_up = nullptr;     // the order is the author's — the switch shows it
	ibButton*               m_down = nullptr;
};

#endif // __CHOICES_DLG_H__
