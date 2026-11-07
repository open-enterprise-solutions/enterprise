#if !defined(_FUNCTIONLIST_H__)
#define _FUNCTIONLIST_H__

#include <wx/sizer.h>
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/checkBox.h"
#include "deskgui/univ/dialog.h"
#include "deskgui/univ/ctrl/listCtrl.h"

// The old desktop's:its module's procedures and functions, and the handlers it may
// have — the module's tab asked for those (ibProtocol::ibView::ibModule::ibRequest::Procedures), its document the desktop's metaobject.
class ibTabDocument;
class ibCodeEditor;

#include <map>

struct ibFunctionList : public ibDialog
{
	wxArrayString aListName;
	wxArrayInt aListImage;

	ibCheckBox *m_Sort;
	ibButton *m_Cancel;
	ibButton *m_OK;

	ibListCtrl *m_listProcedures;

	ibTabDocument *m_docModule;
	ibCodeEditor *m_codeEditor;

	struct offset_proc_t {
		int m_line;
		int m_offset;
	};

	std::map<long, offset_proc_t> m_aOffsets;
	std::map<long, wxString> m_aArgs;   // a handler not written yet: its parameters, as a template writes them

public:

	ibFunctionList(ibTabDocument *moduleDoc, ibCodeEditor* parent);   // standard constructor

	void OnButtonOk(wxCommandEvent &event);
	void OnButtonCancel(wxCommandEvent &event);
	void OnCheckBoxSort(wxCommandEvent &event);
	void OnItemSelected(wxListEvent &event);
};

#endif // !defined(_FUNCTIONLIST_H__)
