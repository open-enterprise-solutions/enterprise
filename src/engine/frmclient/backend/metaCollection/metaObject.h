#ifndef _FRMCLIENT_BACKEND_META_OBJECT_H__
#define _FRMCLIENT_BACKEND_META_OBJECT_H__

#include "frmclient/backend/metaData.h"   // the link — the tab the object was opened from
#include "frmclient/backend/propertyManager/propertyObject.h"   // a metaobject IS a property object, as the engine's

// ⭐ A METAOBJECT OF THE CONFIGURATION — the engine's ibValueMetaObject (backend/metaCollection/metaObject.h), as the
// thin client holds it: ONE CLASS for every kind, no heir. What it is made of — its properties, how they read, what they
// may be set to — is the server's to say (Max, 2026-10-14): it holds only its id and the link to the configuration it
// was opened from (ibMetaData: the tab — two bases open, an edit goes to the one it came from), and fills its
// properties from the server's rows (ibPropertyValue), one per row, kept — an edit running through one outlives the
// answer that brings the rows back. The designer's tree makes one for the row it selects, and drops it for the next
// (ibMetaTreeBase::SelectItem); a ported window that only names the class meets none.
class FRMCLIENT_API ibValueMetaObject : public ibPropertyObject {
public:

	ibValueMetaObject(ibMetaID id, const ibMetaData& metaData) : m_metaId(id), m_metaData(metaData) {}

	template <typename T>
	T* ConvertToType() const { return nullptr; }

	ibMetaID GetMetaID() const { return m_metaId; }
	wxString GetName() const;
	wxString GetSynonym() const;
	wxBitmap GetIcon() const { return wxNullBitmap; }

	// Its properties as the server has them now — one batch (schema Properties: Id, Tab), in place of the ones it holds
	// (ibPropertyObject::ReadProperty). False: the server answered none.
	bool ReadProperty();

	virtual wxString GetClassName() const override { return wxT("metaObject"); }
	virtual wxString GetObjectTypeName() const override { return wxT("metaObject"); }
	virtual bool IsEditable() const override { return true; }

	virtual const ibMetaData* GetMetaData() const override { return &m_metaData; }
	virtual ibMetaData* GetMetaData() override { return &m_metaData; }

	// ⭐ AN EDIT GOES TO THE SERVER — the property named, given the value an edit gave it (its node), on the object of this
	// id in the configuration it was opened from: schema Properties, Save. Refused: vetoed — the value stays.
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) override;

private:

	const ibMetaID m_metaId;
	ibMetaData m_metaData;
};

#endif
