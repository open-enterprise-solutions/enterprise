#ifndef __PROPERTY_CHOICE_LINK_H__
#define __PROPERTY_CHOICE_LINK_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/choiceLinkDescription.h"

// Forward, not included: this header is pulled in by propertyManager.h, which the metadata headers
// themselves include - asking for them back from here is the circle the property family is arranged to
// avoid. The definitions arrive in the .cpp, which is where they are used.
class ibValueMetaObjectAttributeBase;
class ibMetaData;

// ⭐ WHAT GOVERNS THE TYPE OF THIS FIELD — one property holding an ibChoiceTypeLinkDescription, the way
// the type property holds an ibTypeDescription. It answers WHICH LIST OPENS; what is shown inside it is
// the choice parameters' answer, and the two are separate properties because they produce different
// things (docs/private/choice-links.md § 1).
//
// ⭐ WHICH FIELDS MAY GOVERN IS ANSWERED HERE, and nowhere else — the inspector's drop-down and any tool
// that writes this link both ask. Every field beside this one that holds ANYTHING is offered, because a
// link by type is the choice of the COLUMN the type is pulled from (Max, 2026-09-23: "I just point at
// the column I pull it from"), and what is pulled is that column's own `GetTypeValueDesc`.
//
// 🛑 TWO NARROWER RULES WERE TRIED AND BOTH FAILED THE SAME WAY — first "only a characteristic", then
// "only a field whose two type answers differ". Each made the mechanism be ABOUT characteristics, so
// beside a field that had none the list came up empty and the link could not be set at all. A refusal
// nobody can satisfy is not a safeguard. The one thing with nothing to give is an empty declaration
// (ibChoiceLinkResolver::CanGovern).
class BACKEND_API ibPropertyChoiceLink : public ibProperty {
	static wxVariantData* CreateVariantData(ibPropertyObject* property, const ibChoiceTypeLinkDescription& linkDesc = ibChoiceTypeLinkDescription());
public:

	ibChoiceTypeLinkDescription& GetValueAsLinkDesc() const;
	void SetValue(const ibChoiceTypeLinkDescription& val);

	// The fields that may govern this one, as the drop-down offers them.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	ibPropertyChoiceLink(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}

	// NOTHING GOVERNS IT — the field opens the list its own type declares.
	virtual bool IsEmptyProperty() const override { return !GetValueAsLinkDesc().IsOk(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

protected:

	// The family rule — a value arrives in whichever wrapper the caller was handed. See propertyRecord.h.
	virtual void DoSetValue(const wxVariant& val) override;

public:

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

// ⭐ WHAT IS SHOWN IN THE LIST — the table beside the link above, holding an
// ibChoiceParametersDescription. Its rows are edited in a dialog rather than on the inspector's row,
// so this class answers the two questions that dialog's drop-downs ask, and nothing else decides them:
// which parameters a row may fill, and which fields may supply a value.
//
// ⭐ THE OWNER IS A ROW OF THIS TABLE (choiceLinkDescription.h): a catalog declaring `ListOwner` gets
// one written for it, which is why there is no property for the owner anywhere.
class BACKEND_API ibPropertyChoiceParameters : public ibProperty {
	static wxVariantData* CreateVariantData(ibPropertyObject* property, const ibChoiceParametersDescription& paramsDesc = ibChoiceParametersDescription());
public:

	ibChoiceParametersDescription& GetValueAsParametersDesc() const;
	void SetValue(const ibChoiceParametersDescription& val);

	// The fields of what this one refers to — what a row may fill. A composite field answers per type,
	// which is why the type is asked for rather than assumed.
	void GetParameterList(const ibClassID& ofType, ibPropertyChoiceList& list) const;

	// The fields that may supply a value: this field's neighbours in its own holder, one hop each.
	// Anything may supply a VALUE — unlike a link by type, which needs a field that carries a TYPE —
	// so the list is not narrowed.
	void GetSourceList(ibPropertyChoiceList& list) const;

	ibPropertyChoiceParameters(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}

	// NO ROWS — the list shows everything the field's type holds.
	virtual bool IsEmptyProperty() const override { return !GetValueAsParametersDesc().IsOk(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

protected:

	virtual void DoSetValue(const wxVariant& val) override;

public:

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

// ⭐⭐ THE REFERENCE TYPES OF A FIELD, NAMED — ONE ANSWER, TWO ASKERS. The link's editor asks it to
// say which of the field's types a link decides; the parameters dialog asks it because a row belongs
// to one target type. Two names for it (`GetGovernedTypeList`, `GetTargetTypeList`) were the same loop
// written twice, and this file spelled that loop FOUR times in all — each copy free to disagree with
// the others about what counts as a reference (Max, 2026-09-23, reading the header).
//
// ⚠ IT ANSWERS EVERY TYPE, and the "do not ask a question with one answer" rule is the CALLER'S: it
// is a decision about a window, not about the field, and the field's own types do not change because
// there is nobody to show them to.
BACKEND_API void ibFieldReferenceTypes(const ibPropertyObject* owner, ibPropertyChoiceList& list);

// ⭐ THE OWNER'S ROW FOR THIS FIELD, worked out rather than asked for: the target catalog's `ListOwner`
// names the parameter, a neighbouring field of the owner's type supplies its value. False when there is
// nothing to write — the target owns nothing, or no neighbour holds an owner.
//
// 🛑 CALLED WHERE THE TYPE IS SETTLED (ibValueMetaObjectAttribute::OnPropertyChanged), ONCE. A window
// that re-offers it every time it opens cannot tell a table the author emptied from one nobody has
// opened, so the row it "suggests" can never be removed.
BACKEND_API bool ibChoiceOwnerRow(const ibPropertyObject* owner, ibChoiceParameterRowDescription& row);

// (ibChoiceHolderFieldName / ibChoiceTargetFieldName are declared in variant/variantChoiceLink.h — their
//  one caller is the variant that draws the inspector's row, and THIS header is reached through
//  propertyManager.h by the whole metadata tree. Two declarations put here cost a nine-minute rebuild
//  of everything for the sake of one .cpp; where a thing is declared is a build-time decision as much
//  as a design one.)

#endif
