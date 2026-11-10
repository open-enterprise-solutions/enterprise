#ifndef __SERIALIZABLE_VARIANT_H__
#define __SERIALIZABLE_VARIANT_H__

#include "core/variant.h"

#include "core/serialize/dataBuilder.h"   // ibDataValue — the value, as the server stores it

// ⭐ THE ONE VARIANT OF A SERVER'S PROPERTY (Max, 2026-10-20) — what ibPropertySerializable holds, for every type alike:
// its value as the server stores it (the node) and its words as the server wrote them (Presentation). Nothing is
// defined per type to hold it: the presentation of a type takes out of it what it shows (ibPropertyRegistry), and the
// object inspector moves it as it moves any variant, knowing nothing of what is inside. The shape is the server's cells'
// (backend/propertyManager/property/variant/): a class derived from ibVariant, an object held by a value by its count.
class ibVariantSerializable : public ibVariant {
public:

	ibVariantSerializable(const ibDataValue& value, const ibString& presentation)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_value(value), m_presentation(presentation) {}
	ibVariantSerializable(const ibVariantSerializable& src)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_value(src.m_value), m_presentation(src.m_presentation) {}

	const ibDataValue& GetNodeValue() const { return m_value; }

	virtual ibString GetClassName() const override { return s_className; }
	virtual ibString GetString() const override { return m_presentation; }

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantSerializable* const other = data.ConvertToType<ibVariantSerializable>();
		return other != nullptr && other->m_value == m_value;
	}

	// …of a value a cell holds — none: it is not one of these.
	static ibDataValue GetNodeValue(const ibVariant& value) {
		const ibVariantSerializable* const data = value.ConvertToType<ibVariantSerializable>();
		return data != nullptr ? data->m_value : ibDataValue();
	}

	static constexpr const ibChar* s_className = ibT("ibVariantSerializable");

private:

	ibDataValue m_value;          // the value, as the server stores it
	ibString    m_presentation;   // the words it reads as — the server's
};

#endif // !__SERIALIZABLE_VARIANT_H__
