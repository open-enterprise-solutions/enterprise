#ifndef _ATTRIBUTE_CONTROL_H__
#define _ATTRIBUTE_CONTROL_H__

#include "frontend/frontend.h"

#include <vector>

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
	virtual const ibValueMetaObjectAttributeBase* GetSourceAttributeObject() const = 0;
	//////////////////////////////////////////////////

	static bool SimpleChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent);

	static bool QuickChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent);
	static void QuickChoice(ibControlFrame* controlValue, ibValue& newValue, wxWindow* parent, const wxString& strData);

	static ibClassID ShowSelectType(const ibMetaData* metadata, const ibTypeDescription& typeDescription);

	//////////////////////////////////////////////////

	ibSelectMode GetSelectMode() const;
	
	//Create value by selected type
	virtual ibValue CreateValue() const;
	virtual ibValue* CreateValueRef() const;

	//Get data type
	virtual ibClassID GetDataType() const;

	// Bound source path — PURE: every concrete control returns its own (from its source
	// property). A column reads its PARENT tablebox's path to prefix its column id.
	virtual ibSourceDescription GetSourceDesc() const override = 0;

	// GetSourceList is NOT implemented here — each concrete control overrides
	// ibBackendTypeSourceFactory::GetSourceList(out) using its own GetOwnerForm()
	// + GetFilterSourceDataType() (no cross-cast). See widgets / tableBox.
};

#endif