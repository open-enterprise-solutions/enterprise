#ifndef _ATTRIBUTE_CONTROL_H__
#define _ATTRIBUTE_CONTROL_H__

#include "frontend/frontend.h"

///////////////////////////////////////////////////////////////////////////
#include "backend/backend_type.h"
#include "backend/compiler/value.h"
#include "backend/metaCollection/attribute/metaAttributeObjectEnum.h"
///////////////////////////////////////////////////////////////////////////

class BACKEND_API ibMetaData;

class BACKEND_API ibValueMetaObject;
class BACKEND_API ibValueMetaObjectAttributeBase;
class BACKEND_API ibValueMetaObjectGenericData;
class BACKEND_API ibValueMetaObjectTableData;

class BACKEND_API ibSourceDataObject;

///////////////////////////////////////////////////////////////////////////
class FRONTEND_API ibControlFrame;
///////////////////////////////////////////////////////////////////////////

#include "backend/srcObject.h"

class FRONTEND_API ibTypeControlFactory : public ibBackendTypeSourceFactory {
public:

	//////////////////////////////////////////////////
	virtual ibValueMetaObjectAttributeBase* GetSourceAttributeObject() const = 0;
	//////////////////////////////////////////////////

	static bool SimpleChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent);

	static bool QuickChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent);
	static void QuickChoice(ibControlFrame* controlValue, ibValue& newValue, wxWindow* parent, const wxString& strData);

	static ibClassID ShowSelectType(ibMetaData* metadata, const ibTypeDescription& typeDescription);

	//////////////////////////////////////////////////

	ibSelectMode GetSelectMode() const;
	
	//Create value by selected type
	virtual ibValue CreateValue() const;
	virtual ibValue* CreateValueRef() const;

	//Get data type 
	virtual ibClassID GetDataType() const;
};

#endif