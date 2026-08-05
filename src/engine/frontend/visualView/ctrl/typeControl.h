#ifndef _ATTRIBUTE_CONTROL_H__
#define _ATTRIBUTE_CONTROL_H__

#include "frontend/frontend.h"

#include <vector>

///////////////////////////////////////////////////////////////////////////
#include "backend/backend_type.h"
#include "backend/sourceDescription.h"   // ibSourceDescription::GetPath (GetSourceAbstractColumn inline)
#include "backend/compiler/value.h"
#include "backend/metaCollection/metaObjectEnum.h"   // ibSelectMode (GetSelectMode's return type)
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
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const = 0;
	//////////////////////////////////////////////////

	// THE ONE ROUTE every value editor walks when its Select button is pressed:
	//
	//   Undefined      -> GetDataType() decides the type (asking only when the cell
	//                     admits more than one) and the value is created;
	//   typed value    -> the quick choice for that type, and if the type has none,
	//                     the metaobject's own selection form.
	//
	// Written ONCE here rather than in each control: a text control on a form, a
	// table column and a filter cell are the same conversation with the user, and
	// the second copy of it is where they start to differ by accident.
	// `choiceForm` — the selection form the caller wants opened (a text control and a
	// table column each carry one as a property); null = the metaobject's own.
	static bool ChooseValue(ibControlFrame* ownerValue,
		const class ibValueMetaObject* choiceForm = nullptr, wxWindow* parent = nullptr);

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

	// The bound source's PRESENTATION (name / synonym / comment), resolved ONCE: a metadata FIELD -> its
	// column (is-a presentation); a whole FORM ATTRIBUTE (no field column) -> the head attribute (also a
	// presentation). Controls read GetSynonym/GetName/GetComment off it, blind to metadata vs attribute.
	// Inline so it lives in BOTH the desktop and web builds (typeControl.cpp is desktop-only).
	const ibBackendAbstractColumn* GetSourceAbstractColumn() const {
		if (const ibBackendSourceColumn* column = GetSourceAttributeObject())
			return column;
		const ibSourceDescription& desc = GetSourceDesc();
		return desc.IsOk() ? FindSourceHolder(desc.GetFirst()) : nullptr;   // whole-attribute → the head holder
	}

	// Designer: (re)build the control's DEFAULT child controls from its bound source explorer — a
	// tablebox fills its columns (the runtime CreateColumnCollection is DesignerMode-gated, so the
	// designer needs this twin). Default no-op; called after a drag-to-create drop AND by the inspector's
	// Source-change refill — ONE source-explorer traversal point, no duplication.
	virtual void RefillFromSource() { }

	// GetSourceList is NOT implemented here — each concrete control overrides
	// ibBackendTypeSourceFactory::GetSourceList(out) using its own GetOwnerForm()
	// + GetFilterSourceDataType() (no cross-cast). See widgets / tableBox.
};

#endif