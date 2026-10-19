#ifndef __CHOICE_LINK_VARIANT_H__
#define __CHOICE_LINK_VARIANT_H__

#include "frmclient/backend/propertyManager/propertyObject.h"
#include "core/description/choiceLinkDescription.h"

#include <vector>

// THE DESKTOP'S TWO VARIANTS (backend/propertyManager/property/variant/variantChoiceLink.h) — a link by type, holding
// an ibChoiceTypeLinkDescription, and the choice parameters beside it, holding an ibChoiceParametersDescription.
//
// ⚠ NAMED BY THE SERVER, NOT HERE. The desktop's names the fields a description points at by looking them up where
// they live (ibChoiceHolderFieldName / ibChoiceTargetFieldName), and drops a link to a field that is gone
// (RefreshLinkDesc) — both over the configuration, which is the server's. So the text is the row's Presentation,
// carried beside the description, and dropping what is gone stays with the server, which reads what is written.
class ibVariantDataChoiceLink : public wxVariantData {
	wxString MakeString() const { return m_linkDesc.IsOk() ? m_display : wxString(); }
public:

	ibVariantDataChoiceLink(const ibPropertyObject* prop, const ibChoiceTypeLinkDescription& linkDesc = ibChoiceTypeLinkDescription(),
		const wxString& display = wxEmptyString)
		: wxVariantData(), m_ownerProperty(prop), m_linkDesc(linkDesc), m_display(display) {}

	ibChoiceTypeLinkDescription& GetLinkDesc() { return m_linkDesc; }
	const ibChoiceTypeLinkDescription& GetLinkDesc() const { return m_linkDesc; }

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool Eq(wxVariantData& data) const override {
		const ibVariantDataChoiceLink* other = dynamic_cast<const ibVariantDataChoiceLink*>(&data);
		return other != nullptr && GetLinkDesc() == other->GetLinkDesc();
	}

	virtual ibVariantDataChoiceLink* Clone() const override {
		return new ibVariantDataChoiceLink(m_ownerProperty, GetLinkDesc(), m_display);
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override {
		str << MakeString();
		return true;
	}
#endif
	// The governing field's name — what the inspector shows on the row. Empty when nothing governs it.
	virtual bool Write(wxString& str) const override {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataChoiceLink"); }

protected:

	const ibPropertyObject*     m_ownerProperty;
	ibChoiceTypeLinkDescription m_linkDesc;
	wxString                    m_display;   // the row's Presentation — see above
};

// The variant of the choice parameters — the table beside the link by type, in the same shape. Kept in this file
// because it is the same subject: what narrows the choice of a field.
class ibVariantDataChoiceParameters : public wxVariantData {
	wxString MakeString() const { return m_paramsDesc.IsOk() ? m_display : wxString(); }
public:

	ibVariantDataChoiceParameters(const ibPropertyObject* prop, const ibChoiceParametersDescription& paramsDesc = ibChoiceParametersDescription(),
		const wxString& display = wxEmptyString)
		: wxVariantData(), m_ownerProperty(prop), m_paramsDesc(paramsDesc), m_display(display) {}

	ibChoiceParametersDescription& GetParametersDesc() { return m_paramsDesc; }
	const ibChoiceParametersDescription& GetParametersDesc() const { return m_paramsDesc; }

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool Eq(wxVariantData& data) const override {
		const ibVariantDataChoiceParameters* other = dynamic_cast<const ibVariantDataChoiceParameters*>(&data);
		return other != nullptr && GetParametersDesc() == other->GetParametersDesc();
	}

	virtual ibVariantDataChoiceParameters* Clone() const override {
		return new ibVariantDataChoiceParameters(m_ownerProperty, GetParametersDesc(), m_display);
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const override {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataChoiceParameters"); }

protected:

	const ibPropertyObject*       m_ownerProperty;
	ibChoiceParametersDescription m_paramsDesc;
	wxString                      m_display;   // as the link's above
};

// THE PARAMETERS' WINDOW'S TWO QUESTIONS — the desktop's declarations, each a walk over the configuration from this
// field: what it stands in (the object or the tabular section whose other fields are its neighbours), and which fields
// of what it refers to an attribute could fill, the likeliest first, and the one that is obvious. The server answers
// both on the row (its Sources — ibPropertyInspector), so they are asked of the property; a row without them answers
// none: no holder named, nothing an attribute fills.
class FRMCLIENT_API ibBackendProperty;
FRMCLIENT_API wxString ibChoiceHolderName(const ibBackendProperty* property);
FRMCLIENT_API void ibChoiceParametersForSource(const ibBackendProperty* property, const ibMetaID& sourceId,
	std::vector<ibMetaID>& out, ibMetaID* obvious = nullptr);

#endif
