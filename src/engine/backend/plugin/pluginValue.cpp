/////////////////////////////////////////////////////////////////////////////
// ibPluginCallScope — arena-scoped lifetime for opaque ibPluginValue
// objects shared across the plugin ABI boundary.
/////////////////////////////////////////////////////////////////////////////

#include "pluginValue.h"

ibPluginValue* ibPluginCallScope::MakeString(const char* utf8)
{
	auto h = std::make_unique<ibPluginValue_s>();
	h->m_value = ibValue(wxString::FromUTF8(utf8 ? utf8 : ""));
	ibPluginValue* out = h.get();
	m_pool.push_back(std::move(h));
	return out;
}

ibPluginValue* ibPluginCallScope::MakeNumber(double n)
{
	auto h = std::make_unique<ibPluginValue_s>();
	h->m_value = ibValue(n);
	ibPluginValue* out = h.get();
	m_pool.push_back(std::move(h));
	return out;
}

ibPluginValue* ibPluginCallScope::MakeBool(int b)
{
	auto h = std::make_unique<ibPluginValue_s>();
	h->m_value = ibValue(b != 0);
	ibPluginValue* out = h.get();
	m_pool.push_back(std::move(h));
	return out;
}

ibPluginValue* ibPluginCallScope::MakeNull()
{
	auto h = std::make_unique<ibPluginValue_s>();
	// Default-constructed ibValue is TYPE_UNDEFINED — the closest the
	// host has to a "null" sentinel without dragging in a separate Null
	// constructor path.
	ibPluginValue* out = h.get();
	m_pool.push_back(std::move(h));
	return out;
}

ibPluginValue* ibPluginCallScope::AdoptFromValue(const ibValue& v)
{
	auto h = std::make_unique<ibPluginValue_s>();
	h->m_value = v;
	ibPluginValue* out = h.get();
	m_pool.push_back(std::move(h));
	return out;
}

const char* ibPluginCallScope::GetString(const ibPluginValue* v)
{
	if (v == nullptr) return "";
	// Cache the UTF-8 buffer inside the value itself so the returned
	// pointer outlives the temporary wxScopedCharBuffer that
	// wxString::ToUTF8 hands back.
	auto* mut = const_cast<ibPluginValue_s*>(v);
	mut->m_utf8Cache = std::string(v->m_value.GetString().utf8_str());
	return mut->m_utf8Cache.c_str();
}

double ibPluginCallScope::GetNumber(const ibPluginValue* v)
{
	if (v == nullptr) return 0.0;
	return v->m_value.GetDouble();
}

int ibPluginCallScope::GetBool(const ibPluginValue* v)
{
	if (v == nullptr) return 0;
	return v->m_value.GetBoolean() ? 1 : 0;
}

int ibPluginCallScope::IsNull(const ibPluginValue* v)
{
	if (v == nullptr) return 1;
	// Treat the uninitialised + Null states both as "null" for plugin
	// purposes — matches script-side coercion at the call site.
	const auto t = v->m_value.GetType();
	return (t == ibValueTypes::TYPE_EMPTY ||
	        t == ibValueTypes::TYPE_NULL) ? 1 : 0;
}
