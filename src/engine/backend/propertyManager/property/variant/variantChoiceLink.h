#ifndef __CHOICE_LINK_VARIANT_H__
#define __CHOICE_LINK_VARIANT_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/choiceLinkDescription.h"

// The variant of a link by type — it holds an ibChoiceTypeLinkDescription, as ibVariantDataCalcSchedule
// holds a schedule and ibVariantDataAttribute holds a type description.
class BACKEND_API ibVariantDataChoiceLink : public wxVariantData {
	wxString MakeString() const;
public:

	ibVariantDataChoiceLink(const ibPropertyObject* prop, const ibChoiceTypeLinkDescription& linkDesc = ibChoiceTypeLinkDescription())
		: wxVariantData(), m_ownerProperty(prop), m_linkDesc(linkDesc) {}

	ibChoiceTypeLinkDescription& GetLinkDesc() { RefreshLinkDesc(); return m_linkDesc; }
	const ibChoiceTypeLinkDescription& GetLinkDesc() const { RefreshLinkDesc(); return m_linkDesc; }

	// The fields this one may be linked to — asked of the value by its property, as a type's filter is
	// (ibPropertyType::GetFilterDataType).
	ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) const;

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool Eq(wxVariantData& data) const override {
		const ibVariantDataChoiceLink* other = dynamic_cast<const ibVariantDataChoiceLink*>(&data);
		return other != nullptr && GetLinkDesc() == other->GetLinkDesc();
	}

	virtual ibVariantDataChoiceLink* Clone() const override {
		return new ibVariantDataChoiceLink(m_ownerProperty, GetLinkDesc());
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

	// ⭐ A LINK TO A FIELD THAT IS GONE IS DROPPED BY THE VALUE ITSELF — the way a type description drops a
	// type whose object is gone (ibVariantDataAttribute::GetTypeDesc), and asked on the same counter, so a
	// configuration that does not change pays once.
	void RefreshLinkDesc() const;

	const ibPropertyObject*              m_ownerProperty;
	mutable ibChoiceTypeLinkDescription  m_linkDesc;
	mutable unsigned int                 m_object_version = 0;
};

// The variant of the choice parameters — the table beside the link by type, in the same shape. Kept in
// this file because it is the same subject: what narrows the choice of a field.
class BACKEND_API ibVariantDataChoiceParameters : public wxVariantData {
	wxString MakeString() const;
public:

	ibVariantDataChoiceParameters(const ibPropertyObject* prop, const ibChoiceParametersDescription& paramsDesc = ibChoiceParametersDescription())
		: wxVariantData(), m_ownerProperty(prop), m_paramsDesc(paramsDesc) {}

	ibChoiceParametersDescription& GetParametersDesc() { RefreshParametersDesc(); return m_paramsDesc; }
	const ibChoiceParametersDescription& GetParametersDesc() const { RefreshParametersDesc(); return m_paramsDesc; }

	// The two lists a row is made from — the fields of what is chosen, per type, and the fields beside this one.
	void GetParameterList(const ibClassID& ofType, ibPropertyChoiceList& list) const;
	void GetSourceList(ibPropertyChoiceList& list) const;

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool Eq(wxVariantData& data) const override {
		const ibVariantDataChoiceParameters* other = dynamic_cast<const ibVariantDataChoiceParameters*>(&data);
		return other != nullptr && GetParametersDesc() == other->GetParametersDesc();
	}

	virtual ibVariantDataChoiceParameters* Clone() const override {
		return new ibVariantDataChoiceParameters(m_ownerProperty, GetParametersDesc());
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

	// ⭐ A ROW NAMING A FIELD THAT IS GONE IS DROPPED BY THE VALUE ITSELF, as the link above is (Max,
	// 2026-09-24: "if you delete an attribute that was part of the set, that row is simply dropped").
	void RefreshParametersDesc() const;

	const ibPropertyObject*               m_ownerProperty;
	mutable ibChoiceParametersDescription m_paramsDesc;
	mutable unsigned int                  m_object_version = 0;
};

// ⭐⭐ THE NAME OF A FIELD A LINK OR A PARAMETER NAMES — looked for WHERE SUCH A FIELD LIVES, which is
// not the same place for the two: a link names a NEIGHBOUR of this field, a parameter names a field of
// what this one REFERS TO. Two questions, so two names for them.
//
// 🛑 BOTH WERE ASKED OF THE CONFIGURATION AT LARGE (`FindAnyObjectByFilter`), and that finds no
// PREDEFINED field — Ref, Data version, Code, Parent are not ordinary metaobjects. So a row on one of
// them resolved to nothing and was silently DROPPED: the inspector showed an empty property over a
// table that had rows in it (Max, 2026-09-23). Empty is the one answer a summary must never give by
// accident — it cannot be told from "nothing is set".
//
// 🛑 AND THEY ARE DECLARED HERE, beside their one caller, rather than in propertyChoiceLink.h next to
// the property they ask about. That header is reached through propertyManager.h by every metadata
// header, so two declarations in it rebuilt the whole tree — nine and a half minutes for the sake of
// one .cpp (measured 2026-09-23). Where a thing is declared is a build-time decision as much as a
// design one; this header is seen by five files.
//
// Empty only when the field is genuinely gone; the caller then says so in its own words.
BACKEND_API wxString ibChoiceHolderFieldName(const ibPropertyObject* owner, const ibMetaID& id);
BACKEND_API wxString ibChoiceTargetFieldName(const ibPropertyObject* owner, const ibMetaID& id);

// ⭐ WHAT THIS FIELD STANDS IN — the object or the tabular section whose other fields are its
// neighbours. The editor names its two lists by what they hold, and this is the name of the second:
// the left pane says "Fields of Goods" (what is filtered), the value column says which object it is
// filtered BY. Without it a person reads a catalogue's fields on one side and has no idea where the
// values come from (Max, 2026-09-23: "I do not understand where it is pulled from at all").
BACKEND_API wxString ibChoiceHolderName(const ibPropertyObject* owner);

// ⭐⭐ WHICH PARAMETERS AN ATTRIBUTE COULD FILL — every one of them, in the order they are worth
// offering. A parameter is the condition `field of the target = value of this attribute`, so a field
// whose type cannot hold that value never matches ANYTHING: the list comes up empty and the person goes
// looking for the fault everywhere except in that row. An impossible pair is not merely awkward, it is
// a guaranteed wrong answer, and it is not offered at all (Max, 2026-09-23: "show on the left what
// suits us by default — then there is nothing to guess").
//
// `out` empty means this attribute fills nothing here, so it is not offered at all.
//
// ⭐⭐ AND `obvious` IS THE ANSWER WHEN THERE IS ONE — said HERE rather than inferred from the list's
// length, which is how the first cut got it wrong: it filled the name in only when a single candidate
// existed, so a `Code` carried over against a target that also has `Description` and `Article` matched
// its own name and was dropped anyway (Max, 2026-09-23: "why did it not fill itself in?").
//
// It is the field NAMED THE SAME, else the only candidate there is, else 0 — an author who named two
// fields alike has already answered, and that stays true however many others would also fit by type.
BACKEND_API void ibChoiceParametersForSource(const ibPropertyObject* owner, const ibMetaID& sourceId,
	std::vector<ibMetaID>& out, ibMetaID* obvious = nullptr);

#endif
