#ifndef __PROPERTY_SERIALIZABLE_H__
#define __PROPERTY_SERIALIZABLE_H__

#include "frmclient/backend/propertyManager/propertyObject.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — the row it is filled from, ibDataValue — its value

// ⭐ THE THIN CLIENT'S ONE PROPERTY — a server object's property as its row brings it (frmserver
// ibPropertyInspector::AppendProperty): its type (the server's class: what the registry shows it by), its value as the
// server stores it, how it reads (Presentation), what it may be set to. No class per kind: a client that grows one holds
// the server's model a second time, and is a thick one again (Max, 2026-10-14).
//
// Its owner makes it of the row and holds it till the next rows come (ibPropertyObject::Receive).
class FRMCLIENT_API ibPropertySerializable : public ibProperty {
public:

	ibPropertySerializable(ibPropertyCategory* cat, const ibDataNode& row);

	// The metaobject it opens — a module its owner holds (the row's Object); none: 0.
	ibMetaID GetObjectId() const { return m_objectId; }
	// An event's: its procedure's parameters, as the desktop's ibEvent has them (the row's Args); none: empty.
	const wxArrayString& GetArgs() const { return m_args; }

	// …unless the server says it is not (its own IsEditable — the row's ReadOnly).
	virtual bool IsEditable() const override { return !m_readOnly && ibProperty::IsEditable(); }
	// Its type, as the server names it — the key its presentation is registered by.
	virtual wxString GetType() const override { return m_type; }
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
	wxArrayString m_args;
};

#endif
