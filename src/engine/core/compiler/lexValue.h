#ifndef _CORE_LEX_VALUE_H__
#define _CORE_LEX_VALUE_H__

////////////////////////////////////////////////////////////////////////////
// ibLexValue — A VALUE AS TEXT WRITES ONE: Undefined, Null, a flag, a number, a date, a text. Nothing it
// holds is an object, so nothing in it is counted or referred to.
////////////////////////////////////////////////////////////////////////////
//
// The translator's constant (ibLexem::m_valData): a lexer reads values, not references, and with this one
// it needs no runtime under it. The client's value (frmclient/backend/compiler/value.h) is the same
// primitive with what the server sends besides — the one to derive from it. The engine's ibValue takes
// one through its constructor, where the compiler puts a constant into the bytecode.
//
////////////////////////////////////////////////////////////////////////////

#include "core/anyValue.h"

class CORE_API ibLexValue : public ibAnyValue {
public:

	ibLexValue() = default;
	ibLexValue(const wxString& text) : m_typeClass(ibValueTypes::TYPE_STRING), m_text(text) {}
	ibLexValue(const wxChar* text) : m_typeClass(ibValueTypes::TYPE_STRING), m_text(text) {}
	// A narrow literal is a text too — without this one `ibLexValue("x")` is the flag TRUE (pointer-to-bool
	// beats the conversion to wxString).
	ibLexValue(const char* text) : m_typeClass(ibValueTypes::TYPE_STRING), m_text(text) {}
	ibLexValue(bool flag) : m_typeClass(ibValueTypes::TYPE_BOOLEAN), m_text(flag ? wxT("True") : wxT("False")), m_number(flag ? 1 : 0) {}
	ibLexValue(const ibNumber& number);
	ibLexValue(const ibDateTime& date);
	// Undefined or Null — the kinds that hold nothing.
	explicit ibLexValue(ibValueTypes type) : m_typeClass(type) {}

	// Copied without the count — a copy is not held by whoever held the original.
	ibLexValue(const ibLexValue& rhs);
	ibLexValue(ibLexValue&& rhs) noexcept;
	ibLexValue& operator=(const ibLexValue& rhs);
	ibLexValue& operator=(ibLexValue&& rhs) noexcept;

	virtual ibValueTypes GetType() const override { return m_typeClass; }
	virtual bool IsEmpty() const override;

	virtual bool GetBoolean() const override;
	virtual ibNumber GetNumber() const override;
	virtual ibDateTime GetDate() const override;
	virtual ibString GetString() const override;

	// READ FROM THE TEXT THAT SPELLS IT — the engine's: false when the text is not one.
	bool SetBoolean(const wxString& text);
	bool SetNumber(const wxString& text);
	bool SetDate(const wxString& text);
	bool SetString(const wxString& text);

	virtual void IncrRef() override { m_refCount++; }
	virtual void DecrRef() override { if (--m_refCount == 0) delete this; }

protected:

	ibValueTypes m_typeClass = ibValueTypes::TYPE_EMPTY;
	wxString     m_text;            // the text, or how the value reads
	long         m_number = 0;
	ibNumber     m_numeric;
	ibDateTime   m_date;

private:

	unsigned     m_refCount = 0;
};

#endif
