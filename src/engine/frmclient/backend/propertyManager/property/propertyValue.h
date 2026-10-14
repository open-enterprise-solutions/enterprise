#ifndef __PROPERTY_VALUE_H__
#define __PROPERTY_VALUE_H__

#include "frmclient/backend/propertyManager/propertyObject.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the row it is filled from, ibDataValue — its value

// ⭐ THE THIN CLIENT'S ONE PROPERTY — a server object's property as its row brings it (frmserver
// ibPropertyInspector::AppendProperty): its type (the server's class: what the registry shows it by), its value as the
// server stores it, how it reads (Presentation), what it may be set to. No class per kind: a client that grows one holds
// the server's model a second time, and is a thick one again (Max, 2026-10-14).
//
// Its owner fills it and keeps it (ibValueMetaObject::ReadProperty): a row sent again — after every edit — is read
// into the same property, so an edit running through it outlives the answer that brings the rows back.
class FRMCLIENT_API ibPropertyValue : public ibProperty {
public:

	ibPropertyValue(ibPropertyCategory* cat, const ibDataNode& row);

	// The row again, as the server sent it now.
	void ReadProperty(const ibDataNode& row);

	// Its type, as the server names it — the key its presentation is registered by.
	const wxString& GetType() const { return m_type; }
	bool IsReadOnly() const { return m_readOnly; }
	// The metaobject it opens — a module its owner holds (the row's Object); none: 0.
	ibMetaID GetObjectId() const { return m_objectId; }

	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	// Not a value of the engine's — the server holds that.
	virtual bool SetDataValue(const ibValue& WXUNUSED(varPropVal)) override { return false; }
	virtual bool GetDataValue(ibValue& WXUNUSED(pvarPropVal)) const override { return false; }

protected:

	// Its value as the server stores it — read as it is, written back as it is.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

private:

	wxString m_type;
	ibDataValue m_value;
	ibPropertyChoiceList m_choices;
	ibPropertyChoiceMode m_mode = ibPropertyChoiceMode::None;
	bool m_readOnly = false;
	ibMetaID m_objectId = 0;
};

#endif
