#ifndef __PROPERTY_POINT_H__
#define __PROPERTY_POINT_H__

#include "backend/propertyManager/propertyObject.h"
#include "core/graphics/geometry.h"   // ibPoint — the point, as data

//base property for "point"
class BACKEND_API ibPropertyPoint : public ibProperty {
	static ibVariant CreateVariantData(const ibPoint& val);
public:

	ibPoint GetValueAsPoint() const;
	ibString GetValueAsString() const { return GetValueAsPoint().ToString(); }

	void SetValue(const ibPoint& val) { m_propValue = CreateVariantData(val); }
	void SetValue(const ibString& val) { ibPoint point = ibDefaultPosition; point.FromString(val); SetValue(point); }

	ibPropertyPoint(ibPropertyCategory* cat, const ibString& name, const ibPoint& p = ibDefaultPosition)
		: ibProperty(cat, name, CreateVariantData(p))
	{
	}

	ibPropertyPoint(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibPoint& p = ibDefaultPosition)
		: ibProperty(cat, name, label, CreateVariantData(p))
	{
	}

	ibPropertyPoint(ibPropertyCategory* cat, const ibString& name, const ibString& label, const ibString& helpString, const ibPoint& p = ibDefaultPosition)
		: ibProperty(cat, name, label, helpString, CreateVariantData(p))
	{
	}

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal);
	virtual bool GetDataValue(ibValue& pvarPropVal) const;

	//load & save object in control 

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;

public:

};

#endif