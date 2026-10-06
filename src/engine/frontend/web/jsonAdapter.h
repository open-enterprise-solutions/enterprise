#ifndef __JSON_ADAPTER_H__
#define __JSON_ADAPTER_H__

// Lets nlohmann::json accept wxString directly:
//
//     j["key"] = m_value;          // m_value is wxString
//     wxString s = j["key"];       // round-trip
//
// Without this every assignment had to read
// `std::string(s.mb_str(wxConvUTF8))` — that pattern is gone now.

#include <wx/string.h>

#include <nlohmann/json.hpp>

namespace nlohmann {

template <>
struct adl_serializer<wxString> {
	static void to_json(json& j, const wxString& s) {
		const auto buf = s.ToUTF8();
		j = std::string(buf.data(), buf.length());
	}

	static void from_json(const json& j, wxString& s) {
		const auto& str = j.get_ref<const std::string&>();
		s.AssignFromUTF8(str.c_str(), str.size());
	}
};

} // namespace nlohmann

#endif
