#ifndef __META_INTERFACE_H__
#define __META_INTERFACE_H__

#include "metaObject.h"

class BACKEND_API CMetaObjectInterface : public IMetaObject {
	wxDECLARE_DYNAMIC_CLASS(CMetaObjectModule);
protected:
	enum
	{
		ID_METATREE_OPEN_INTERFACE = 19000,
	};
public:

	CMetaObjectInterface(const wxString& name = wxEmptyString, const wxString& synonym = wxEmptyString, const wxString& comment = wxEmptyString);

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//prepare menu for item
	virtual bool PrepareContextMenu(wxMenu* defaultMenu);
	virtual void ProcessCommand(unsigned int id);
};

#endif 