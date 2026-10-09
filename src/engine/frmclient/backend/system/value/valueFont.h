#ifndef _FRMCLIENT_BACKEND_VALUE_FONT_H__
#define _FRMCLIENT_BACKEND_VALUE_FONT_H__

#include "frmclient/backend/compiler/value.h"

// A FONT AS A VALUE — the engine's (backend/system/value/valueFont.h): what an appearance's parameter keeps, written as
// the engine writes it — its class, and its text (typeConv) — so the server reads it back as its own.
class FRMCLIENT_API ibValueFont : public ibValue {
public:

	wxFont m_font;

	ibValueFont() = default;
	ibValueFont(const wxFont& font) : m_font(font) {}

	virtual ibClassID GetClassType() const override { return value_to_clsid("VL_FONT"); }
	virtual ibString GetString() const override;
	virtual bool IsEmpty() const override { return !m_font.IsOk(); }

protected:

	virtual bool DoSerialize(class ibDataNode& node) const override;
	virtual bool DoDeserialize(const class ibDataNode& node) override;
};

#endif
