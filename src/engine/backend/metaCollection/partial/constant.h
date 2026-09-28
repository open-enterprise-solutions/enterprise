#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#include "backend/metaCollection/metaStoredValueObject.h"

// THE CONSTANT — a stored value whose TYPE its author chooses. Everything a stored value is (its column in
// sys_const, its form, its rights, its record module, the read and write doors) is the base's
// (metaStoredValueObject.h); a constant adds the two settings of its value.
class BACKEND_API ibValueMetaObjectConstant : public ibValueMetaObjectStoredValue {
public:

	ibValueMetaObjectConstant() : ibValueMetaObjectStoredValue() {}

	//support icons
	virtual wxIcon GetIcon() const override;
	static wxIcon GetIconGroup();

	// --- the type facade — it stayed HERE, which is what the user edits ------------------------
	//
	// The type is the constant's own property, exactly as it was when the constant was an attribute:
	// same property class, same editor, same place in the inspector. The value column reads it back
	// through GetTypeDesc, so the type the user picks and the type the DDL renders are one value,
	// not two that have to be kept in step.
	virtual ibTypeDescription& GetTypeDesc() const override { return m_propertyType->GetValueAsTypeDesc(); }
	virtual bool FillCheck() const override { return m_propertyFillCheck->GetValueAsBoolean() && GetTypeDesc().GetClsidCount() > 0; }

protected:

	//per-type node data
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

private:

	// The VALUE properties — moved here from the attribute base the constant used to inherit. They
	// are what the user edits, so they belong on the object the designer shows, and the inner column
	// reads them back rather than holding a second copy.
	ibPropertyCategory* m_categoryValue = ibPropertyObject::CreatePropertyCategory(wxT("Value"), _("Value"));
	ibPropertyType* m_propertyType = ibPropertyObject::CreateProperty<ibPropertyType>(m_categoryValue, wxT("Type"), _("Type"), _("The type of the constant's value - a primitive (string, number, date, boolean) or a reference. The one stored value is kept in the system table of constants and converted to this type when read."), ibValueTypes::TYPE_STRING);
	ibPropertyBoolean* m_propertyFillCheck = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryValue, wxT("FillCheck"), _("Fill check"), _("Refuse to write an empty value: the constant's form shows the field as required, and a write leaving it empty fails with a message naming it."));
};

#endif
