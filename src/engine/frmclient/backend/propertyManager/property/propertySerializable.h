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
//
// ⭐ ITS VALUE IS ONE VARIANT, THE SAME FOR EVERY TYPE (ibVariantDataSerializable, Max, 2026-10-20) — the node the server
// stores it as, named by its class: what is read is held as it comes, what is written is what is held. Whoever sets and
// gets it moves a variant and knows nothing of the node. The client's own properties (a sheet's cell's) hold theirs so
// too: each is one of these, made of no row, set by the type it is.
class FRMCLIENT_API ibPropertySerializable : public ibProperty {
public:

	ibPropertySerializable(ibPropertyCategory* cat, ibDataNode&& row);

	// …unless the server says it is not (its own IsEditable — the row's ReadOnly).
	virtual bool IsEditable() const override { return !m_readOnly && ibProperty::IsEditable(); }
	// Its type, as the server names it — the key its presentation is registered by; the client's own, its class.
	virtual wxString GetType() const override { return !m_type.IsEmpty() ? m_type : ibProperty::GetType(); }
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	// The row it was made of, as the server wrote it; the client's own, as it writes one.
	virtual bool GetProtocolValue(ibDataNode& row) const override;

protected:

	// The client's own — made of no row (a sheet's cell's).
	ibPropertySerializable(ibPropertyCategory* cat, const wxString& name, const wxVariant& value) : ibProperty(cat, name, value) {}
	ibPropertySerializable(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxVariant& value) : ibProperty(cat, name, label, value) {}
	ibPropertySerializable(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString, const wxVariant& value) : ibProperty(cat, name, label, helpString, value) {}

	// A node arriving (a typed setter's, the gate's), held as it comes, named by its class. A node has no words: what
	// reads here is the old ones until the rows come back.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

private:

	ibDataNode m_row;
	wxString m_type;
	ibPropertyChoiceList m_choices;
	ibPropertyChoiceMode m_mode = ibPropertyChoiceMode::None;
	bool m_readOnly = false;
};

#endif
