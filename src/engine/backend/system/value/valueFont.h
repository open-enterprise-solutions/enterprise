#ifndef __VALUE_FONT_H__
#define __VALUE_FONT_H__

#include "backend/compiler/value.h"
#include "core/graphics/font.h"   // ibFont — the font, as data

//Array support
void ibValueFont_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

class BACKEND_API ibValueFont : public ibValueStaticMembers<&ibValueFont_BindNames>
{
	public:
	ibFont m_font;
public:

	ibValueFont();
	ibValueFont(const ibFont& font);
	virtual ~ibValueFont() {}

	virtual bool Init(ibValue** paParams, const long lSizeArray);

	virtual ibString GetString() const {
		return m_font.ToString();
	}

	//check is empty
	virtual bool IsEmpty() const {
		return !m_font.IsOk();
	}

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value
	// DoGetPMethods (protected) + Shared<&ibValueFont_BindNames> come from the base.

	// ⭐ A PACKED FORM — the text a property keeps a font in (ibFont's), as the colour has one.
	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;
};

#endif