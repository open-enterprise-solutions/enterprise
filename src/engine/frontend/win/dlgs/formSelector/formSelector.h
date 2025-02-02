#ifndef _FORM_SELECTOR_H__
#define _FORM_SELECTOR_H__

#include <wx/dialog.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/radiobox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <map>

#include "frontend/frontend.h"

class IMetaObjectForm;

class FRONTEND_API CDialogSelectTypeForm : public wxDialog {
	
	IMetaObjectForm* m_metaObject;
	wxStaticText* m_staticTextName;
	wxStaticText* m_staticTextSynonym;
	wxStaticText* m_staticTextComment;
	wxTextCtrl* m_textCtrlName;
	wxTextCtrl* m_textCtrlSynonym;
	wxTextCtrl* m_textCtrlComment;
	wxButton* m_sdbSizerOK;
	wxButton* m_sdbSizerCancel;

	struct choiceData_t {
		wxString m_name;
		wxString m_label;
		int		 m_value;
		choiceData_t(const wxString& name, const wxString& label,
			const int& val) : m_name(name), m_label(label), 
			m_value(val) 
		{
		}
	};

	std::vector<choiceData_t> m_aChoices;
	unsigned int m_choice;

public:

	CDialogSelectTypeForm(class IMetaObject* metaValue, IMetaObjectForm* metaObject);
	virtual ~CDialogSelectTypeForm();

	void CreateSelector();

	void AppendTypeForm(const wxString& name, const wxString &label, int index) {
		m_aChoices.emplace_back(name, label, index);
	}

protected:

	void OnTextEnter(wxCommandEvent& event);
	void OnButtonOk(wxCommandEvent& event);
	void OnButtonCancel(wxCommandEvent& event);
	void OnFormTypeChanged(wxCommandEvent& event);
};

#endif 