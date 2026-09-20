#pragma once

// The arithmetic behind the calculator that a number field offers (the "..." beside a sum).
//
// It is an ordinary four-function pocket calculator — digits, a point, + - * /, equals, a sign change,
// backspace, clear — and nothing else: no window, no wx GUI. What it works in is ibNumber, the exact
// decimal the field itself holds, not a double, so 0.1 + 0.2 is 0.3 and a sum of money never picks up
// a stray digit on the way from the calculator into the field.
//
// The window is a thin skin over it (frontend/visualView/ctrl/typeControl.cpp); keeping the rules here is
// what lets them be tested without one.

#include "backend/fnumber.h"

#include <wx/string.h>

class ibNumberCalculator {
public:

	enum class Op : char { None = 0, Add = '+', Subtract = '-', Multiply = '*', Divide = '/' };

	// A quotient has no end in general (1 / 3); it is cut here, half away from zero, and a value that
	// was cut is still exact in every place a person can read.
	static constexpr int kResultScale = 10;

	ibNumberCalculator() { Reset(ibNumber(0)); }
	explicit ibNumberCalculator(const ibNumber& initial) { Reset(initial); }

	// Start from a value — the one already in the field. The next digit typed replaces it (as on any
	// calculator that shows a result), while an operator carries on from it.
	void Reset(const ibNumber& initial)
	{
		m_entry.clear();
		m_accumulator = initial;
		m_pending = Op::None;
		m_typing = false;
		m_error = false;
	}

	// ---------------------------------------------------------------- what is on the display

	bool HasError() const { return m_error; }

	// What the display shows: the number being typed exactly as typed ("0.", "12.50"), else the
	// accumulated value in its shortest exact form.
	wxString Display() const
	{
		if (m_error)
			return wxT("Error");
		if (m_typing)
			return m_entry.empty() ? wxString(wxT("0")) : m_entry;
		return Format(m_accumulator);
	}

	// The value the display stands for — what goes into the field on OK. While an operation is
	// still open ("12 +" and nothing typed after it) it is the accumulated 12; ending on an
	// operator never invents an operand.
	ibNumber Value() const
	{
		if (m_error)
			return ibNumber(0);
		return m_typing ? EntryValue() : m_accumulator;
	}

	// The open operation, for a line under the display ("12 +").
	Op Pending() const { return m_pending; }

	// ---------------------------------------------------------------- keys

	void Digit(int digit)
	{
		if (digit < 0 || digit > 9)
			return;
		if (m_error)
			Reset(ibNumber(0));
		if (!m_typing)
			BeginEntry();

		// A leading zero is not kept ("007" is 7), and typing has a length: the field it ends up in
		// is a number of a declared size, not an unbounded string of digits.
		if (m_entry == wxT("0"))
			m_entry.clear();
		else if (m_entry == wxT("-0"))
			m_entry = wxT("-");

		if (DigitCount() >= kMaxDigits)
			return;
		m_entry += wxString::Format(wxT("%d"), digit);
	}

	void Point()
	{
		if (m_error)
			Reset(ibNumber(0));
		if (!m_typing)
			BeginEntry();
		if (m_entry.Find(wxT('.')) != wxNOT_FOUND)
			return;
		if (m_entry.empty() || m_entry == wxT("-"))
			m_entry += wxT("0");
		m_entry += wxT('.');
	}

	void Backspace()
	{
		if (m_error) {
			Reset(ibNumber(0));
			return;
		}
		if (!m_typing)
			return;   // a result is not typed text; there is nothing to take a character off
		if (!m_entry.empty())
			m_entry.RemoveLast();
		if (m_entry == wxT("-"))
			m_entry.clear();
	}

	void Negate()
	{
		if (m_error)
			return;
		if (m_typing) {
			if (m_entry.StartsWith(wxT("-")))
				m_entry.Remove(0, 1);
			else
				m_entry.Prepend(wxT("-"));
			return;
		}
		m_accumulator = -m_accumulator;
	}

	// C: everything back to zero.
	void Clear() { Reset(ibNumber(0)); }

	void Operator(Op op)
	{
		if (m_error || op == Op::None)
			return;

		// "12 + 3 *" finishes 12 + 3 first, the way a pocket calculator does; two operators in a row
		// ("12 + *") replace one another, they do not combine.
		if (m_typing) {
			if (m_pending != Op::None)
				Apply();
			else
				m_accumulator = EntryValue();
			m_typing = false;
			m_entry.clear();
		}
		if (m_error)
			return;
		m_pending = op;
	}

	void Equals()
	{
		if (m_error)
			return;
		if (m_typing) {
			if (m_pending != Op::None)
				Apply();
			else
				m_accumulator = EntryValue();
			m_typing = false;
			m_entry.clear();
		}
		m_pending = Op::None;
	}

private:

	static constexpr int kMaxDigits = 20;

	void BeginEntry()
	{
		m_typing = true;
		m_entry.clear();
	}

	int DigitCount() const
	{
		int n = 0;
		for (const wxUniChar c : m_entry)
			if (c >= wxT('0') && c <= wxT('9'))
				++n;
		return n;
	}

	ibNumber EntryValue() const
	{
		if (m_entry.empty() || m_entry == wxT("-"))
			return ibNumber(0);
		ibNumber value;
		wxString text = m_entry;
		if (text.EndsWith(wxT(".")))
			text.RemoveLast();   // "12." is 12
		value.FromString(text);
		return value;
	}

	// accumulator = accumulator <pending> entry
	void Apply()
	{
		const ibNumber right = EntryValue();
		switch (m_pending) {
		case Op::Add:      m_accumulator += right; break;
		case Op::Subtract: m_accumulator -= right; break;
		case Op::Multiply: m_accumulator *= right; break;
		case Op::Divide:
			if (right.IsZero()) {
				m_error = true;
				return;
			}
			m_accumulator /= right;
			break;
		case Op::None: break;
		}
		m_accumulator = m_accumulator.Round(kResultScale);
	}

	// The shortest exact spelling: no trailing zeros after the point, no bare point.
	static wxString Format(const ibNumber& value)
	{
		wxString text = value.ToString();
		if (text.Find(wxT('.')) != wxNOT_FOUND) {
			while (text.EndsWith(wxT("0")))
				text.RemoveLast();
			if (text.EndsWith(wxT(".")))
				text.RemoveLast();
		}
		if (text == wxT("-0") || text.empty())
			text = wxT("0");
		return text;
	}

	wxString m_entry;
	ibNumber m_accumulator;
	Op       m_pending = Op::None;
	bool     m_typing = false;
	bool     m_error = false;
};
