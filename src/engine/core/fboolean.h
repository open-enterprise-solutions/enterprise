#ifndef __FBOOLEAN_H__
#define __FBOOLEAN_H__

// =============================================================================
// ibBoolean (fboolean.h) — the engine's flag: ONE BYTE, and the ONE place it is
// spelled. A value keeps it in its union beside ibNumber, ibDateTime and ibString
// (backend/compiler/value.h); the translator's constant (ibVariant) and its
// keywords write it with the same words, so `True` cannot read one way in a
// script and another in a text, on the server and on the client.
//
// No members named True / False: X11 defines both as macros.
// =============================================================================


#include "core/core.h"   // CORE_API

class ibString;          // core/fstring.h - the flag's text; the family meets in backend_core.h

// THE FLAG AS THE LANGUAGE WRITES IT — what ibBoolean writes and reads, and the translator's keywords.
inline constexpr const ibChar* g_booleanTrue  = ibT("True");
inline constexpr const ibChar* g_booleanFalse = ibT("False");

class CORE_API ibBoolean
{
public:
	constexpr ibBoolean() noexcept = default;
	constexpr ibBoolean(bool flag) noexcept : m_flag(flag) {}

	constexpr operator bool() const noexcept { return m_flag; }

	// READ FROM A TEXT: `True` / `False` in any case, the spaces around it aside. False when the text is
	// neither — and the flag is then false.
	bool FromString(const ibString& text);
	ibString ToString() const;

private:
	bool m_flag = false;
};

static_assert(sizeof(ibBoolean) == 1, "a flag is one byte — it sits in ibValue's union");

#endif
