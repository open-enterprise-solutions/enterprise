#if !defined(_FUNCTIONLIST_H__)
#define _FUNCTIONLIST_H__

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>

class CDocument;
class CCodeEditorCtrl;

#include <map>

struct CFunctionList : public wxDialog
{
	wxArrayString aListName;
	wxArrayInt aListImage;

	wxCheckBox *m_Sort;
	wxButton *m_Cancel;
	wxButton *m_OK;

	wxListCtrl *m_listProcedures;

	CDocument *m_docModule;
	CCodeEditorCtrl *m_codeEditor;

	struct offset_proc_t {
		int m_line;
		int m_offset;
	};

	std::map<long, offset_proc_t> m_aOffsets; 

public:

	CFunctionList(CDocument *moduleDoc, CCodeEditorCtrl* parent);   // standard constructor

	void OnButtonOk(wxCommandEvent &event);
	void OnButtonCancel(wxCommandEvent &event);
	void OnCheckBoxSort(wxCommandEvent &event);
	void OnItemSelected(wxListEvent &event);
};

#endif // !defined(_FUNCTIONLIST_H__)
