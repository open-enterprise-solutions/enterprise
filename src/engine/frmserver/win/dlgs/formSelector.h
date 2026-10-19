#ifndef _FORM_SELECTOR_H__
#define _FORM_SELECTOR_H__

#include <vector>

#include <wx/string.h>

class ibValueMetaObject;
class ibValueMetaObjectFormBase;

// THE KIND OF A FORM JUST MADE — the desktop's ibDialogSelectTypeForm (frontend/win/dlgs/formSelector), its window the
// designer's (designer-thin/win/dlgs/formSelector): the kinds and the names they give are sent
// (ibProtocol::ibRequest::ibKind::FormType), the kind chosen and the form's Name, Synonym and Comment the person typed come back
// and are put on the form, as the desktop's OK put them. The configuration's tree asks it as the desktop's does
// (ibMetaTreeBase::AskFormKind).
class ibDialogSelectTypeForm {
public:

	ibDialogSelectTypeForm(ibValueMetaObject* metaValue, ibValueMetaObjectFormBase* metaObject);

	void AppendTypeForm(const wxString& name, const wxString& label, int index) {
		m_listChoice.emplace_back(name, label, index);
	}

	// The kind chosen — a generic form's is wxNOT_FOUND (defaultFormType); false — cancelled, or nobody to ask.
	bool ShowModal(int& chosen);

private:

	struct choiceData_t {
		wxString m_name;
		wxString m_label;
		int      m_value;
		choiceData_t(const wxString& name, const wxString& label, int val) : m_name(name), m_label(label), m_value(val) {}
	};

	ibValueMetaObject*         m_metaValue;
	ibValueMetaObjectFormBase* m_metaObject;

	std::vector<choiceData_t> m_listChoice;
};

#endif
