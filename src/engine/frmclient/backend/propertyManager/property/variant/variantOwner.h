#ifndef _FRMCLIENT_BACKEND_VARIANT_OWNER_H__
#define _FRMCLIENT_BACKEND_VARIANT_OWNER_H__

#include <wx/variant.h>

#include "frmclient/backend/backend_core.h"

class ibPropertyObject;

// WHICH METAOBJECT — the engine's ibMetaDescription (backend/typeDescription.h), by its id.
struct ibMetaDescription {
	explicit ibMetaDescription(ibMetaID id = 0) : m_id(id) {}
	ibMetaID m_id;
};

// A RELATIONSHIP AS A PROPERTY HOLDS IT — the engine's ibVariantDataOwner: the metaobject it names. A relationship is
// the designer's — a configuration's — so the client's properties hold none; it is what listing one would build.
class ibVariantDataOwner : public wxVariantData {
public:

	ibVariantDataOwner(const ibPropertyObject* WXUNUSED(prop), const ibMetaDescription& typeDesc) : m_typeDesc(typeDesc) {}

	virtual bool Eq(wxVariantData& data) const override {
		const ibVariantDataOwner* const other = dynamic_cast<const ibVariantDataOwner*>(&data);
		return other != nullptr && other->m_typeDesc.m_id == m_typeDesc.m_id;
	}
	virtual wxString GetType() const override { return wxT("ibVariantDataOwner"); }

private:

	ibMetaDescription m_typeDesc;
};

#endif
