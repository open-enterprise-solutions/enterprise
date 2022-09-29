#ifndef __META_INTERFACE_H__
#define __META_INTERFACE_H__

#include "metaObject.h"

class CMetaInterfaceObject : public IMetaObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaModuleObject);
protected:
	enum
	{
		ID_METATREE_OPEN_INTERFACE = 19000,
	};
public:

	CMetaInterfaceObject(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	virtual wxString GetClassName() const override {
		return wxT("interface");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);
};

#endif 