#ifndef __PROPERTY_RECORD_H__
#define __PROPERTY_RECORD_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/backend_type.h"

//base property for "record"
class BACKEND_API ibPropertyRecord : public ibProperty {
	static wxVariantData* CreateVariantData(ibPropertyObject* property, const ibMetaDescription& typeDesc = ibMetaDescription());
public:

	ibMetaDescription& GetValueAsMetaDesc() const;
	ibMetaDescription& GetValueAsMetaDesc(const wxVariant& val) const;
	void SetValue(const ibMetaDescription& val);

	// WHICH REGISTERS A DOCUMENT POSTS TO — information, accumulation and accounting registers.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	ibPropertyRecord(ibPropertyCategory* cat, const wxString& name) : ibProperty(cat, name, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyRecord(ibPropertyCategory* cat, const wxString& name, const wxString& label) : ibProperty(cat, name, label, CreateVariantData(cat->GetPropertyObject())) {}
	ibPropertyRecord(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString) : ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

	// Which KINDS this property offers is said once, in GetValueList, through the shared
	// CreateValueList — a sibling below says its own and inherits everything else.

protected:

	// 🛑 A RELATIONSHIP IS THE SAME FACT IN FIVE WRAPPERS, AND THIS ONE KEEPS ITS OWN. Every family
	// here holds an ibMetaDescription; they differ only in the class that carries it. So a value
	// arriving from a NEIGHBOUR of the family is not a wrong value — it says exactly the right
	// thing — but storing it as it comes leaves this property holding a variant it cannot read,
	// and everything downstream raises "its value is not of the expected kind": the delete path,
	// the diff, the save. One mis-set property made a whole configuration unsaveable.
	//
	// ⚠ AND IT ARRIVED THE HONEST WAY: CreateValueList builds every candidate as ibVariantDataOwner,
	// so a caller that did the right thing — read GetValueList, pick, place the value it was
	// offered — was handed the neighbour's wrapper by the property system itself.
	//
	// So the property TAKES THE DESCRIPTION AND WRAPS IT ITSELF. Anything of the family is accepted;
	// anything else falls through to the base, which stores it as before.
	virtual void DoSetValue(const wxVariant& val) override;

public:

};

// ⭐⭐ THE SEQUENCES A DOCUMENT REGISTERS IN — a list of its own, BESIDE the registers it posts
// movements to and not among them (Max, 2026-09-18: "последовательности живут у себя… есть
// последовательность, а есть движение"). Two doors, because they are two acts: a movement is what the
// document DID, a registration is that the document STANDS in an order — and a solution turns them on
// and off separately.
//
// Everything else about the two is the same, so this is the record property with ONE answer changed:
// which kinds it offers. That answer lives in GetValueList and nowhere else.
class BACKEND_API ibPropertySequenceRecord : public ibPropertyRecord {
public:
	using ibPropertyRecord::ibPropertyRecord;

	// Only sequences, and only those a document can be the recorder of — the same rule the registers'
	// list keeps, asked of the candidate rather than assumed.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;
};

#endif