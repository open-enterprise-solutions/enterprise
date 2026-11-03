#ifndef _KIND_SELECTOR_H__
#define _KIND_SELECTOR_H__

#include <vector>

#include "core/fstring.h"

class ibValueMetaObject;

// THE KIND OF AN OBJECT JUST MADE — a form's, as the old desktop's ibDialogSelectTypeForm asked it, and a template's
// (2026-11-03); its window the client's (frmclient/win/dlgs/kindSelector): the kinds and the names they give are sent
// (ibProtocol::ibRequest::ibKind::ObjectKind), the kind chosen and the object's Name, Synonym and Comment the person
// typed come back and are put on the object, as the desktop's OK put them. The configuration's tree asks it as the
// desktop's does (ibMetaTreeBase::AskFormKind, AskTemplateKind), and names the window.
class ibDialogSelectKind {
public:

	ibDialogSelectKind(ibValueMetaObject* metaObject, const ibString& caption);

	// A kind offered — the stem of the name the object takes with it, what the person reads, and its id.
	void AppendKind(const ibString& name, const ibString& label, int id) {
		m_listChoice.emplace_back(name, label, id);
	}

	// The kind chosen — a generic form's is wxNOT_FOUND (defaultFormType); false — cancelled, or nobody to ask.
	bool ShowModal(int& chosen);

private:

	struct ibChoiceData {
		ibString m_name;
		ibString m_label;
		int      m_value;
		ibChoiceData(const ibString& name, const ibString& label, int val) : m_name(name), m_label(label), m_value(val) {}
	};

	ibValueMetaObject* m_metaObject;
	ibString           m_caption;

	std::vector<ibChoiceData> m_listChoice;
};

#endif
