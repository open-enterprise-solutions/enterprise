#ifndef __CHOICE_LINK_VARIANT_H__
#define __CHOICE_LINK_VARIANT_H__

#include "core/variant.h"
#include "backend/propertyManager/propertyObject.h"
#include "core/description/choiceLinkDescription.h"
#include "backend/srcColumn.h"   // ibBackendSourceColumn — what a field beside this one is (ibChoiceHolderField)

#include <atomic>
#include <mutex>

// The variant of a link by type — it holds an ibChoiceTypeLinkDescription, as ibVariantCalcSchedule
// holds a schedule and ibVariantAttribute holds a type description.
class BACKEND_API ibVariantChoiceLink : public ibVariant {
public:

	ibVariantChoiceLink(const ibPropertyObject* prop, const ibChoiceTypeLinkDescription& linkDesc = ibChoiceTypeLinkDescription())
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_linkDesc(linkDesc) {}
	ibVariantChoiceLink(const ibVariantChoiceLink& src)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(src.m_ownerProperty), m_linkDesc(src.GetLinkDesc()) {}

	ibChoiceTypeLinkDescription& GetLinkDesc() { RefreshLinkDesc(); return m_linkDesc; }
	const ibChoiceTypeLinkDescription& GetLinkDesc() const { RefreshLinkDesc(); return m_linkDesc; }

	// The fields this one may be linked to — asked of the value by its property, as a type's filter is
	// (ibPropertyType::GetFilterDataType).
	ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) const;

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantChoiceLink* other = dynamic_cast<const ibVariantChoiceLink*>(data.GetRef());
		return other != nullptr && GetLinkDesc() == other->GetLinkDesc();
	}

	// The governing field's name — what the inspector shows on the row. Empty when nothing governs it.
	virtual ibString GetString() const override;

protected:

	// ⭐ A LINK TO A FIELD THAT IS GONE IS DROPPED BY THE VALUE ITSELF — the way a type description drops a
	// type whose object is gone (ibVariantAttribute::GetTypeDesc), and asked on the same counter, so a
	// configuration that does not change pays once.
	void RefreshLinkDesc() const;

	const ibPropertyObject*              m_ownerProperty;
	mutable ibChoiceTypeLinkDescription  m_linkDesc;
	// One refresh at a time, the version asked again inside the lock — read by every session of the base
	// (ibVariantAttribute, the same counter and the same race).
	mutable std::atomic<unsigned int>    m_object_version { 0 };
	mutable std::mutex                   m_refreshMutex;
};

// The variant of the choice parameters — the table beside the link by type, in the same shape. Kept in
// this file because it is the same subject: what narrows the choice of a field.
class BACKEND_API ibVariantChoiceParameters : public ibVariant {
public:

	ibVariantChoiceParameters(const ibPropertyObject* prop, const ibChoiceParametersDescription& paramsDesc = ibChoiceParametersDescription())
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_paramsDesc(paramsDesc) {}
	ibVariantChoiceParameters(const ibVariantChoiceParameters& src)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(src.m_ownerProperty), m_paramsDesc(src.GetParametersDesc()) {}

	ibChoiceParametersDescription& GetParametersDesc() { RefreshParametersDesc(); return m_paramsDesc; }
	const ibChoiceParametersDescription& GetParametersDesc() const { RefreshParametersDesc(); return m_paramsDesc; }

	// The fields of what is chosen, per type — what a row may fill. Where its value comes from is one of the fields
	// beside this one (ibChoiceHolderFields).
	void GetParameterList(const ibClassID& ofType, ibPropertyChoiceList& list) const;

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantChoiceParameters* other = dynamic_cast<const ibVariantChoiceParameters*>(data.GetRef());
		return other != nullptr && GetParametersDesc() == other->GetParametersDesc();
	}

	virtual ibString GetString() const override;

protected:

	// ⭐ A ROW NAMING A FIELD THAT IS GONE IS DROPPED BY THE VALUE ITSELF, as the link above is (Max,
	// 2026-09-24: "if you delete an attribute that was part of the set, that row is simply dropped").
	void RefreshParametersDesc() const;

	const ibPropertyObject*               m_ownerProperty;
	mutable ibChoiceParametersDescription m_paramsDesc;
	mutable std::atomic<unsigned int>     m_object_version { 0 };   // as the link's above
	mutable std::mutex                    m_refreshMutex;
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
BACKEND_API ibString ibChoiceHolderFieldName(const ibPropertyObject* owner, const ibSourceDescription& path);
BACKEND_API ibString ibChoiceTargetFieldName(const ibPropertyObject* owner, const ibMetaID& id);

// ⭐⭐ A FIELD STANDING BESIDE THIS ONE — the PATH a row or a link stores, the COLUMN it is (its name, synonym, type and
// picture are the column's: an attribute of the configuration, a form's own attribute and a field reached through one
// all are one), and how it is SPELLED from here. One shape for every owner, so the lists, the summaries and MCP read
// the same fields the same way (#616).
struct ibChoiceHolderField {
	ibSourceDescription          m_path;
	const ibBackendSourceColumn* m_column = nullptr;
	ibString                     m_spelled;
};

// …and all of them: an attribute's — its holder's other attributes, and for a column the header's; a form's own
// attribute's — the form's other ones; a CONTROL's — what it reaches through the form: the form's attributes, the fields
// of what each holds one step through, and for a column the other cells of its row. Never the field itself.
//
// ⚠ NOT NARROWED, and that is the difference from the link by type (ibVariantChoiceLink::GetValueList): a link needs a
// field that CARRIES a type, a parameter needs a VALUE, and any field has one. Narrowing this would refuse the ordinary
// case of a date or a number standing in a condition.
BACKEND_API std::vector<ibChoiceHolderField> ibChoiceHolderFields(const ibPropertyObject* owner);

// ⭐ WHAT THIS FIELD STANDS IN — the object or the tabular section whose other fields are its
// neighbours. The editor names its two lists by what they hold, and this is the name of the second:
// the left pane says "Fields of Goods" (what is filtered), the value column says which object it is
// filtered BY. Without it a person reads a catalogue's fields on one side and has no idea where the
// values come from (Max, 2026-09-23: "I do not understand where it is pulled from at all").
BACKEND_API ibString ibChoiceHolderName(const ibPropertyObject* owner);

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
BACKEND_API void ibChoiceParametersForSource(const ibPropertyObject* owner, const ibChoiceHolderField& source,
	std::vector<ibMetaID>& out, ibMetaID* obvious = nullptr);

#endif
