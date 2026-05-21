/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardApplier — implementation. See header.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizardApplier.h"

#include <wx/log.h>

#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

#include "backend/plugin/metaBridge.h"
#include "3rdparty/nlohmann/json.hpp"
#include "templateWizardPayload.h"

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

static std::string LowerAscii(std::string s)
{
	for (char& ch : s) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}
	return s;
}

static std::string NormalizedKind(std::string kind)
{
	const std::string lower = LowerAscii(kind);
	if (lower == "enum") return "Enumeration";
	if (lower == "commonmodule") return "CommonModule";
	if (lower == "commonform") return "CommonForm";
	if (lower == "commontemplate") return "CommonTemplate";
	if (lower == "dimension") return "Dimension";
	if (lower == "resource") return "Resource";
	if (kind.empty()) return kind;
	kind[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(kind[0])));
	return kind;
}

static std::string NormalizedOp(const nlohmann::json& m)
{
	std::string op = ibTemplateWizardPayload::StringField(
	    m, "op", "operation", "action");
	if (op.empty()) return "create";
	op = LowerAscii(op);
	if (op == "meta.create" || op == "createobject") return "create";
	if (op == "meta.edit"   || op == "update")       return "edit";
	if (op == "meta.delete" || op == "remove")       return "delete";
	if (op == "insert") return "data-insert";
	return op;
}

static bool IsConfigurationMutation(const std::string& kind,
                                    const std::string& fullName)
{
	return LowerAscii(kind) == "configuration" ||
	       LowerAscii(ibTemplateWizardPayload::InferKindFromFullName(fullName)) ==
	           "configuration";
}

static void PushChildMutation(std::vector<nlohmann::json>& out,
                              const std::string& parentFullName,
                              const char* container,
                              const char* kind,
                              const nlohmann::json& item,
                              const char* fallbackFormType = nullptr)
{
	if (!item.is_object() && !item.is_string()) return;
	nlohmann::json props = nlohmann::json::object();
	std::string name;
	if (item.is_string()) {
		name = item.get<std::string>();
	} else {
		name = ibTemplateWizardPayload::StringField(item, "name");
		props = item;
		if (fallbackFormType != nullptr && !props.contains("formType")) {
			props["formType"] = fallbackFormType;
		}
		if (props.contains("kind") && !props.contains("formType") &&
		    std::string(kind) == "Form" && props["kind"].is_string()) {
			props["formType"] = props["kind"].get<std::string>();
		}
	}
	if (name.empty()) return;
	nlohmann::json child;
	child["op"] = "create";
	child["kind"] = kind;
	child["fullName"] = parentFullName + "." + container + "." + name;
	child["properties"] = props;
	out.push_back(std::move(child));
}

static void ExpandTemplateMutation(const nlohmann::json& m,
                                   std::vector<nlohmann::json>& out)
{
	if (!m.is_object()) return;
	out.push_back(m);
	const std::string op = NormalizedOp(m);
	if (op != "create") return;
	std::string fullName =
	    ibTemplateWizardPayload::StringField(m, "fullName", "name", "path");
	if (fullName.empty()) return;
	const nlohmann::json* props = nullptr;
	if (m.contains("properties")) props = &m["properties"];
	else if (m.contains("props")) props = &m["props"];
	else if (m.contains("definition")) props = &m["definition"];
	if (props == nullptr || !props->is_object()) return;

	if (props->contains("attributes") && (*props)["attributes"].is_array()) {
		for (const auto& item : (*props)["attributes"]) {
			PushChildMutation(out, fullName, "Attributes", "Attribute", item);
		}
	}
	if (props->contains("dimensions") && (*props)["dimensions"].is_array()) {
		for (const auto& item : (*props)["dimensions"]) {
			PushChildMutation(out, fullName, "Dimensions", "Dimension", item);
		}
	}
	if (props->contains("resources") && (*props)["resources"].is_array()) {
		for (const auto& item : (*props)["resources"]) {
			PushChildMutation(out, fullName, "Resources", "Resource", item);
		}
	}
	if (props->contains("forms") && (*props)["forms"].is_array()) {
		for (const auto& item : (*props)["forms"]) {
			PushChildMutation(out, fullName, "Forms", "Form", item, "Form");
		}
	}
	if (props->contains("tabularSections") &&
	    (*props)["tabularSections"].is_array()) {
		for (const auto& item : (*props)["tabularSections"]) {
			PushChildMutation(out, fullName, "TabularSections",
			                  "TabularSection", item);
		}
	}
}

static int CountDemoRows(const nlohmann::json& payload)
{
	if (!payload.contains("demoData") || !payload["demoData"].is_array()) {
		return 0;
	}
	int total = 0;
	for (const auto& di : payload["demoData"]) {
		if (!di.is_object()) continue;
		if (di.contains("rows") && di["rows"].is_array()) {
			total += static_cast<int>(di["rows"].size());
		}
	}
	return total;
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
	const nlohmann::json* mutations =
	    ibTemplateWizardPayload::PickMutations(*payload);
	if (mutations == nullptr || mutations->empty()) {
		OpResult op;
		op.op    = wxT("(empty)");
		op.error = wxT("no template mutations in response");
		result.ops.push_back(op);
		result.failureCount = 1;
		return result;
	}

	std::vector<nlohmann::json> expanded;
	for (const auto& m : *mutations) {
		ExpandTemplateMutation(m, expanded);
	}

	// Walk mutations sequentially. Per spec we prefer partial-apply over
	// all-or-nothing: each metaBridge mutation is individually undoable
	// via Ctrl+Z, so a failure mid-walk leaves the user with a recoverable
	// state.
	for (const auto& m : expanded) {
		OpResult op;
		if (!m.is_object()) {
			op.op    = wxT("(skip)");
			op.error = wxT("non-object entry in mutations[]");
			result.ops.push_back(op);
			++result.failureCount;
			continue;
		}
		const std::string opStr = NormalizedOp(m);
		std::string fullName =
		    ibTemplateWizardPayload::StringField(m, "fullName", "name", "path");
		std::string kindStr =
		    ibTemplateWizardPayload::StringField(m, "kind", "type");
		if (kindStr.empty()) {
			kindStr = ibTemplateWizardPayload::InferKindFromFullName(fullName);
		}
		kindStr = NormalizedKind(kindStr);
		const nlohmann::json* propsObj = nullptr;
		if (m.contains("properties")) propsObj = &m["properties"];
		else if (m.contains("props")) propsObj = &m["props"];
		else if (m.contains("definition")) propsObj = &m["definition"];
		const std::string props = propsObj != nullptr ? propsObj->dump()
		                                              : std::string("{}");

		op.op       = wxString::FromUTF8(opStr.c_str());
		op.kind     = wxString::FromUTF8(kindStr.c_str());
		op.fullName = wxString::FromUTF8(fullName.c_str());

		if (IsConfigurationMutation(kindStr, fullName)) {
			op.op = wxT("configure");
			op.success = true;
			++result.successCount;
			result.ops.push_back(op);
			continue;
		}

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

	if (includeData) result.skippedDataRows = CountDemoRows(*payload);

	return result;
}

}  // namespace ibTemplateWizardApplier
