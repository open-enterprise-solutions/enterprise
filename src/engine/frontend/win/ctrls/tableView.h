#ifndef __DATA_VIEW_BOX_H__
#define __DATA_VIEW_BOX_H__

#include "dataview/dataview.h"

class FRONTEND_API ibTableViewCtrl :
	public ibDataViewCtrl {
public:

	ibTableViewCtrl() : ibDataViewCtrl() {}
	ibTableViewCtrl(wxWindow* parent, wxWindowID id,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize, long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(ibDataViewCtrlNameStr)) : ibDataViewCtrl(parent, id, pos, size, style, validator, name)
	{
	}

	// Open the List-Settings window (Filter / Sort / Group) for the model.
	virtual bool ShowListSettings(class ibValueModel* model) wxOVERRIDE;
	virtual bool ShowViewMode();

private:

	wxDECLARE_DYNAMIC_CLASS(ibTableViewCtrl);
	wxDECLARE_NO_COPY_CLASS(ibTableViewCtrl);
};

#endif 