/////////////////////////////////////////////////////////////////////////////
// Template wizard JSON payload helpers.
/////////////////////////////////////////////////////////////////////////////

#ifndef _OES_TEMPLATE_WIZARD_PAYLOAD_H_
#define _OES_TEMPLATE_WIZARD_PAYLOAD_H_

#include <string>
#include <utility>

#include "3rdparty/nlohmann/json.hpp"

namespace ibTemplateWizardPayload {

inline const nlohmann::json* PickArrayField(const nlohmann::json& payload,
                                            const char* key)
{
	if (payload.contains(key) && payload[key].is_array()) {
		return &payload[key];
	}
	return nullptr;
}

inline const nlohmann::json* PickNestedArray(const nlohmann::json& payload,
                                             const char* objectKey,
                                             const char* arrayKey)
{
	if (!payload.contains(objectKey) || !payload[objectKey].is_object()) {
		return nullptr;
	}
	return PickArrayField(payload[objectKey], arrayKey);
}

inline const nlohmann::json* PickMutations(const nlohmann::json& payload)
{
	const char* direct[] = {
	    "structureMutations",
	    "metadataMutations",
	    "structure",
	    "mutations",
	    "objects",
	};
	const std::pair<const char*, const char*> nested[] = {
	    { "plan",         "mutations" },
	    { "plan",         "structure" },
	    { "template",     "mutations" },
	    { "template",     "structure" },
	    { "mutationPlan", "mutations" },
	};
	for (const char* key : direct) {
		if (const auto* arr = PickArrayField(payload, key)) return arr;
	}
	for (const auto& item : nested) {
		if (const auto* arr = PickNestedArray(payload, item.first, item.second)) {
			return arr;
		}
	}
	return nullptr;
}

inline std::string StringField(const nlohmann::json& obj,
                               const char* a,
                               const char* b = nullptr,
                               const char* c = nullptr)
{
	const char* keys[] = { a, b, c };
	for (const char* key : keys) {
		if (key == nullptr) continue;
		if (obj.contains(key) && obj[key].is_string()) {
			return obj[key].get<std::string>();
		}
	}
	return std::string();
}

inline std::string InferKindFromFullName(const std::string& fullName)
{
	const size_t dot = fullName.find('.');
	return dot == std::string::npos ? std::string() : fullName.substr(0, dot);
}

} // namespace ibTemplateWizardPayload

#endif // _OES_TEMPLATE_WIZARD_PAYLOAD_H_
