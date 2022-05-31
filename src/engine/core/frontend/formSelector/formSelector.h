#ifndef _FORM_SELECTOR_H__
#define _FORM_SELECTOR_H__

#include <wx/dialog.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/radiobox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <map>

class IMetaFormObject;

class CSelectTypeForm : public wxDialog
{
	IMetaFormObject *m_metaObject;

	wxStaticText* m_staticTextName;
	wxStaticText* m_staticTextSynonym;
	wxStaticText* m_staticTextComment;

	wxTextCtrl* m_textCtrlName;
	wxTextCtrl* m_textCtrlSynonym;
	wxTextCtrl* m_textCtrlComment;

	wxButton* m_sdbSizerOK;
	wxButton* m_sdbSizerCancel;

	struct choiceData_t
	{
		wxString m_name;
		unsigned int m_choice;

		choiceData_t(const wxString &name, unsigned int choice) : m_name(name), m_choice(choice) {}
	};

	std::vector<choiceData_t> m_aChoices;
	unsigned int m_choice;

public:

	CSelectTypeForm(class IMetaObject*metaValue, IMetaFormObject *metaObject);
	virtual ~CSelectTypeForm();

	void CreateSelector();

	void AppendTypeForm(const wxString &name, unsigned int formType) {
		m_aChoices.emplace_back(name, formType);
	}

protected:

	void OnTextEnter(wxCommandEvent& event);

	void OnButtonOk(wxCommandEvent& event);
	void OnButtonCancel(wxCommandEvent& event);

	void OnFormTypeChanged(wxCommandEvent& event);
};

#endif 