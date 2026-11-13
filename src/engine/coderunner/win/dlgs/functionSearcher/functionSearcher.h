#if !defined(_FUNCTIONLIST_H__)
#define _FUNCTIONLIST_H__

#include <wx/sizer.h>
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/checkBox.h"
#include "deskgui/univ/dialog.h"
#include "deskgui/univ/ctrl/listCtrl.h"

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

	ibCodeEditor *m_codeEditor;

	struct offset_proc_t {
		int m_line;
		int m_offset;
	};

	std::map<long, offset_proc_t> m_aOffsets; 

public:

	ibFunctionList(ibCodeEditor* parent);   // standard constructor

	void OnButtonOk(wxCommandEvent &event);
	void OnButtonCancel(wxCommandEvent &event);
	void OnCheckBoxSort(wxCommandEvent &event);
	void OnItemSelected(wxListEvent &event);
};

#endif // !defined(_FUNCTIONLIST_H__)
