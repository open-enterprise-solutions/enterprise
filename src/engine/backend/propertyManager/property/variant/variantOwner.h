#ifndef __OWNER_VARIANT_H__
#define __OWNER_VARIANT_H__

#include "backend/metaCollection/partial/commonObject.h"
#include "backend/propertyManager/property/variant/variantMetaDesc.h"

class BACKEND_API ibVariantDataOwner : public ibVariantDataMetaDesc {
	wxString MakeString() const;
public:

	ibValue GetDataValue() const;

	ibVariantDataOwner(const ibPropertyObject* prop, const ibMetaDescription& typeDesc) : ibVariantDataMetaDesc(prop, typeDesc) {}
	ibVariantDataOwner(const ibVariantDataOwner& src) : ibVariantDataMetaDesc(src.m_ownerProperty, src.m_metaDesc) {}

	virtual ibVariantDataOwner* Clone() const {
		return new ibVariantDataOwner(*this);
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const { return wxT("ibVariantDataOwner"); }
};

// ⭐⭐ THE CANDIDATES FOR A RELATIONSHIP — every metaobject of the classes that may fill it, each
// carrying the variant that would set it.
//
// 🛑 THIS FUNCTION EXISTED FIVE TIMES, IN THE FRONT. `advpropOwner`, `advpropRecord`,
// `advpropGeneration`, `advpropChartOfAccounts`, `advpropChartOfCharacteristicTypes` each carried
// their own copy of it, identical but for the clsid list, in the constructor of a wxPGProperty. Two
// consequences, and the second is the expensive one: nothing headless could ever answer "what may
// this be set to" — not the MCP server, not a daemon, not a test — and the answer lived where the
// question could not be asked. The clsid list is a fact about the PROPERTY, not about its editor.
//
// Here, the property answers, the editor reads it back, and there is one copy of the walk.
// `accept` narrows the classes further when belonging to one is not enough. A document posts only to
// registers that HAVE A RECORDER, and that rule lived in the front editor's fill loop — carrying the
// clsids down without it would have offered registers a document cannot post to at all: a wider list
// than the designer's own, which is the quiet kind of wrong.
inline ibPropertyChoiceMode CreateValueList(ibPropertyObject* owner, ibPropertyChoiceList& list,
                                            ibPropertyChoiceMode mode,
                                            const std::initializer_list<ibClassID> classes,
                                            const std::function<bool(ibValueMetaObject*)>& accept = nullptr)
{
	// The cast is at HOME here — this file is about this variant and knows what fills it. What was
	// wrong was the same cast in a generic tool that had no business knowing.
	const ibValueMetaObjectGenericData* factory = dynamic_cast<const ibValueMetaObjectGenericData*>(owner);
	if (factory == nullptr)
		return ibPropertyChoiceMode::None;

	const ibMetaData* metaData = factory->GetMetaData();
	if (metaData == nullptr)
		return ibPropertyChoiceMode::None;

	for (ibValueMetaObject* object : metaData->GetAnyArrayObject(classes)) {

		if (accept != nullptr && !accept(object))
			continue;

		// ⭐ THE VARIANT IS BUILT WHILE LISTING, so a caller that picked an item has nothing left to
		// work out: it places this. A single metaID, because one choice is one relationship — a
		// Mult-mode property composes the set from the ones that were picked.
		list.Add((long)object->GetMetaID(), object->GetName(), object->GetSynonym(),
			wxVariant(new ibVariantDataOwner(factory, ibMetaDescription(object->GetMetaID()))),
			object->GetIcon());
	}

	// THE MODE EVEN WHEN THE LIST CAME OUT EMPTY. "There is nothing of that kind in this
	// configuration yet" is a real and useful answer — it is what a caller sees before the first
	// chart of accounts exists — and it is NOT the same as "this property is not chosen from a
	// list". Collapsing the two would make an empty configuration look like an unsupported property.
	return mode;
}

#endif