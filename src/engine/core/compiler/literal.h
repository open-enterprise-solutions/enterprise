#ifndef _CORE_LITERAL_H__
#define _CORE_LITERAL_H__

////////////////////////////////////////////////////////////////////////////
// ibLiteral — A VALUE AS TEXT WRITES ONE: Undefined, Null, a flag, a number, a date, a text. Nothing it
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
#include "core/fboolean.h"   // g_booleanTrue / g_booleanFalse — the flag's words

class CORE_API ibLiteral : public ibAnyValue {
public:

	ibLiteral() = default;
	ibLiteral(const wxString& text) : m_typeClass(ibValueTypes::TYPE_STRING), m_text(text) {}
	ibLiteral(const ibChar* text) : m_typeClass(ibValueTypes::TYPE_STRING), m_text(text) {}
	// A narrow literal is a text too — without this one `ibLiteral("x")` is the flag TRUE (pointer-to-bool
	// beats the conversion to wxString).
	ibLiteral(const char* text) : m_typeClass(ibValueTypes::TYPE_STRING), m_text(text) {}
	ibLiteral(bool flag) : m_typeClass(ibValueTypes::TYPE_BOOLEAN), m_text(flag ? g_booleanTrue : g_booleanFalse), m_number(flag ? 1 : 0) {}
	ibLiteral(const ibNumber& number);
	ibLiteral(const ibDateTime& date);
	// Undefined or Null — the kinds that hold nothing.
	explicit ibLiteral(ibValueTypes type) : m_typeClass(type) {}

	// Copied without the count — a copy is not held by whoever held the original.
	ibLiteral(const ibLiteral& rhs);
	ibLiteral(ibLiteral&& rhs) noexcept;
	ibLiteral& operator=(const ibLiteral& rhs);
	ibLiteral& operator=(ibLiteral&& rhs) noexcept;

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
