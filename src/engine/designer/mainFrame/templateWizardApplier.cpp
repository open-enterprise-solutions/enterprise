/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardApplier — implementation. See header.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizardApplier.h"

#include <wx/log.h>

#include <cstdlib>
#include <string>

#include "backend/plugin/metaBridge.h"
#include "3rdparty/nlohmann/json.hpp"

namespace ibTemplateWizardApplier {

// Wizard-scoped pluginId. The wizard pre-grants AllowAlways for the
// three meta.* ops before calling Apply; on close it restores the
// pre-existing policy. That keeps the metaBridge audit trail explicit
// (logs show "designer.templateWizard performed meta.create on …").
static const char* s_kWizardPluginId = "designer.templateWizard";

// Unwrap result/structuredContent if present.
static const nlohmann::json* UnwrapPayload(const nlohmann::json& root)
{
	if (root.contains("result") && root["result"].is_object()) {
		return &root["result"];
	}
	if (root.contains("structuredContent") && root["structuredContent"].is_object()) {
		return &root["structuredContent"];
	}
	return &root;
}

// Look up structure[] or mutations[] inside payload.
static const nlohmann::json* PickMutations(const nlohmann::json& payload)
{
	if (payload.contains("structure") && payload["structure"].is_array()) {
		return &payload["structure"];
	}
	if (payload.contains("mutations") && payload["mutations"].is_array()) {
		return &payload["mutations"];
	}
	return nullptr;
}

ApplyResult Apply(const wxString& responseJson, bool includeData)
{
	ApplyResult result;
	auto parsed = nlohmann::json::parse(std::string(responseJson.utf8_str()),
	                                       nullptr, /*allow_exceptions=*/false);
	if (parsed.is_discarded() || !parsed.is_object()) {
		OpResult op;
		op.op    = wxT("(parse)");
		op.error = wxT("template payload not parseable as JSON");
		result.ops.push_back(op);
		result.failureCount = 1;
		return result;
	}
	const nlohmann::json* payload = UnwrapPayload(parsed);
	const nlohmann::json* mutations = PickMutations(*payload);
	if (mutations == nullptr || mutations->empty()) {
		OpResult op;
		op.op    = wxT("(empty)");
		op.error = wxT("no mutations[] / structure[] in template response");
		result.ops.push_back(op);
		result.failureCount = 1;
		return result;
	}

	// Walk mutations sequentially. Per spec we prefer partial-apply over
	// all-or-nothing: each metaBridge mutation is individually undoable
	// via Ctrl+Z, so a failure mid-walk leaves the user with a recoverable
	// state.
	for (const auto& m : *mutations) {
		OpResult op;
		if (!m.is_object()) {
			op.op    = wxT("(skip)");
			op.error = wxT("non-object entry in mutations[]");
			result.ops.push_back(op);
			++result.failureCount;
			continue;
		}
		const std::string opStr   = m.value("op",       std::string("create"));
		const std::string kindStr = m.value("kind",     std::string());
		const std::string fullName= m.value("fullName", std::string());
		const std::string props   = m.contains("properties")
		                              ? m["properties"].dump()
		                              : std::string("{}");

		op.op       = wxString::FromUTF8(opStr.c_str());
		op.kind     = wxString::FromUTF8(kindStr.c_str());
		op.fullName = wxString::FromUTF8(fullName.c_str());

		char* err = nullptr;
		int rc = -1;
		if (opStr == "create") {
			rc = metaBridge::HostMetaCreate(s_kWizardPluginId,
			                                  kindStr.c_str(),
			                                  fullName.c_str(),
			                                  props.c_str(),
			                                  &err);
		} else if (opStr == "edit") {
			rc = metaBridge::HostMetaEdit(s_kWizardPluginId,
			                                fullName.c_str(),
			                                props.c_str(),
			                                &err);
		} else if (opStr == "delete") {
			rc = metaBridge::HostMetaDelete(s_kWizardPluginId,
			                                  fullName.c_str(),
			                                  props.c_str(),
			                                  &err);
		} else {
			op.error = wxT("unknown op '") +
			            wxString::FromUTF8(opStr.c_str()) + wxT("'");
		}
		if (rc == 0) {
			op.success = true;
			++result.successCount;
		} else {
			op.success = false;
			if (err != nullptr) {
				op.error = wxString::FromUTF8(err);
			} else if (op.error.IsEmpty()) {
				op.error = wxString::Format(wxT("rc=%d"), rc);
			}
			++result.failureCount;
		}
		if (err != nullptr) std::free(err);
		result.ops.push_back(op);
	}

	// Demo data — walk demoData[] when requested. Each row becomes a
	// best-effort create inside the matching Catalog/Document. Provider
	// shape is {kind, fullName, rows:[{...}], postAfterInsert?}. We emit
	// one meta.create per row using fullName; provider-specific row-key
	// disambiguation stays server-side.
	if (includeData &&
	    payload->contains("demoData") &&
	    (*payload)["demoData"].is_array()) {
		for (const auto& di : (*payload)["demoData"]) {
			if (!di.is_object()) continue;
			const std::string kind     = di.value("kind",     std::string());
			const std::string fullName = di.value("fullName", std::string());
			if (kind.empty() || fullName.empty()) continue;
			if (!di.contains("rows") || !di["rows"].is_array()) continue;
			for (const auto& row : di["rows"]) {
				if (!row.is_object()) continue;
				OpResult op;
				op.op       = wxT("data-insert");
				op.kind     = wxString::FromUTF8(kind.c_str());
				op.fullName = wxString::FromUTF8(fullName.c_str());
				const std::string props = row.dump();
				char* err = nullptr;
				const int rc = metaBridge::HostMetaCreate(
				    s_kWizardPluginId,
				    kind.c_str(),
				    fullName.c_str(),
				    props.c_str(),
				    &err);
				if (rc == 0) {
					op.success = true;
					++result.successCount;
				} else {
					op.success = false;
					op.error = err != nullptr
					    ? wxString::FromUTF8(err)
					    : wxString::Format(wxT("rc=%d"), rc);
					++result.failureCount;
				}
				if (err != nullptr) std::free(err);
				result.ops.push_back(op);
			}
		}
	}

	return result;
}

}  // namespace ibTemplateWizardApplier
