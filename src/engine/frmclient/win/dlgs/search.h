#ifndef __FRMCLIENT_DIALOG_SEARCH_H__
#define __FRMCLIENT_DIALOG_SEARCH_H__

#include <vector>

#include "core/localization/dictionary.h"   // _() — the dialog's words
#include "deskgui/univ/dialog.h"
#include "protocol/protocolNode.h"
#include "frmclient/visualView/ctrl/frame.h"                  // ibControlFrame — the field a value is chosen for
#include "frmclient/visualView/ctrl/typeControl.h"            // ibTypeControlFactory — …of the column's type
#include "frmclient/win/dlgs/settings/settingsFieldTree.h"    // ibSettingsPlainField — a column, as the server wrote it

class ibChoice;
class ibTextCtrl;
class ibRadioBox;

// A TABLE'S ADVANCED SEARCH — a settings window over one condition (ibRequest::ibSearch): where to look — the columns, as
// a settings window's fields; what for — a value of the column's type, chosen as a settings window's value is
// (ibTypeControlFactory::ChooseValue: a number's, a date's, a flag's here, a value of the server's type asked of the
// server on the way), or a text; how — by the value, from the start of the line, anywhere in it, the whole of it. A column
// holding no text is looked in by its value alone. Find answers the three, Close answers nothing.
class ibDialogSearch : public ibDialog, public ibControlFrame, public ibTypeControlFactory {
public:

	// ibControlFrame — the value looked for By value, what a choice settles on and puts back.
	virtual bool GetControlValue(ibValue& value) const override { value = m_held; return true; }
	virtual bool SetControlValue(const ibValue& value = ibValue()) override;
	virtual void ControlIncrRef() override {}
	virtual void ControlDecrRef() override {}
	virtual bool HasQuickChoice() const override;
	virtual void ChoiceProcessing(ibValue& selected) override;

	// ibTypeControlFactory — the type of the column picked; nothing bound behind it.
	virtual ibTypeDescription& GetTypeDesc() const override { return m_typeDesc; }
	virtual const ibMetaData* GetMetaData() const override;
	virtual ibSourceObject* GetSourceObject() const override { return nullptr; }
	virtual ibSourceDescription& GetSourceDesc() const override { return m_sourceDesc; }

	// Asked over `parent`, answered into `response`: Column, How, Value — the text, or the value ({t, v}) By value;
	// nothing when it is closed.
	static void Show(wxWindow* parent, const ibProtocolNode& request, ibProtocolNode& response);

private:

	ibDialogSearch(wxWindow* parent, const ibProtocolNode& request);

	// The column picked: the value of the one before let go, the ways it is looked in enabled.
	void OnColumn(wxCommandEvent& event);
	// How to look switched: the field shows what that way looks for.
	void OnHow(wxCommandEvent& event);
	// The field's «…»: its value chosen; «×»: the empty one of the column's type.
	void OnSelect(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);

	// Looked for by the value — the way chosen is By value.
	bool ByValue() const;
	// What the field holds now, taken into what the way chosen looks for: the text — as typed; the value — a number, a
	// date, a text typed read as the column's type, a value chosen as it stands.
	void TakeField();
	// …and the field drawn anew for the way chosen: the value's presentation with «…» and «×», or the text.
	void ShowField();

	ibChoice*                         m_column = nullptr;
	ibTextCtrl*                       m_value = nullptr;
	ibRadioBox*                       m_how = nullptr;
	std::vector<ibSettingsPlainField> m_fields;   // the columns, by their places in m_column
	std::vector<ibValue>              m_rowValues;   // what the current row holds in each, by its place — Undefined: none
	ibValue                           m_held;     // what By value looks for — Undefined: nothing
	wxString                          m_text;     // what the other ways look for
	bool                              m_byValue = false;   // the way the field is drawn for
	// The type of the column picked (OnColumn) and no source — mutable as the interface hands them out (rowValueCell's).
	mutable ibTypeDescription         m_typeDesc;
	mutable ibSourceDescription       m_sourceDesc;
};

#endif
