#ifndef _TABLE_NONIFIER_H_
#define _TABLE_NONIFIER_H_ 

#include <wx/dataview.h>

class wxTableModelNotifier {
	wxDataViewCtrl* m_mainWindow;
public:
	wxTableModelNotifier(class wxDataViewCtrl* mainWindow) :
		m_mainWindow(mainWindow) {
	}

	wxDataViewColumn* GetCurrentColumn() const;
	void StartEditing(const wxDataViewItem& item, unsigned int col) const;

	bool SendEvent(const wxEventType& eventType, const wxDataViewItem& item);
	bool SendEvent(const wxEventType& eventType, const wxDataViewItem& item, wxDataViewColumn* column);

	void Select(const wxDataViewItem& item) const;
	wxDataViewItem GetSelection() const;
	int GetSelections(wxDataViewItemArray& sel) const;
};

wxDECLARE_EVENT(wxEVT_DATAVIEW_ITEM_START_DELETING, wxDataViewEvent);

#endif 