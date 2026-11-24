#ifndef __CHOICES_VARIANT_H__
#define __CHOICES_VARIANT_H__

#include "core/variant.h"
#include "backend/propertyManager/propertyObject.h"
#include "core/description/choicesDescription.h"

// The variant of a switch's items — it holds an ibChoicesDescription, as ibVariantCalcSchedule holds its schedule.
class BACKEND_API ibVariantChoices : public ibVariant {
public:

	ibVariantChoices(const ibPropertyObject* prop, const ibChoicesDescription& choicesDesc = ibChoicesDescription())
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_choicesDesc(choicesDesc) {}
	ibVariantChoices(const ibVariantChoices& src)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(src.m_ownerProperty), m_choicesDesc(src.m_choicesDesc) {}

	ibChoicesDescription& GetChoicesDesc() { return m_choicesDesc; }
	const ibChoicesDescription& GetChoicesDesc() const { return m_choicesDesc; }

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantChoices* other = dynamic_cast<const ibVariantChoices*>(data.GetRef());
		return other != nullptr && m_choicesDesc == other->m_choicesDesc;
	}

	// The row the inspector shows — the items as the switch shows them (propertyChoices.cpp).
	virtual ibString GetString() const override;

protected:

	const ibPropertyObject* m_ownerProperty;
	ibChoicesDescription    m_choicesDesc;
};

#endif
