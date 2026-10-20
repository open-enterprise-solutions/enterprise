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

	// …unless the server says it is not (its own IsEditable — the row's ReadOnly).
	virtual bool IsEditable() const override { return !m_readOnly && ibProperty::IsEditable(); }
	// Its type, as the server names it — the key its presentation is registered by.
	virtual wxString GetType() const override { return m_type; }
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	// The row it was made of, as the server wrote it.
	virtual bool GetProtocolValue(ibDataNode& row) const override { row = m_row; return true; }

private:

	ibDataNode m_row;
	wxString m_type;
	ibPropertyChoiceList m_choices;
	ibPropertyChoiceMode m_mode = ibPropertyChoiceMode::None;
	bool m_readOnly = false;
};

#endif
