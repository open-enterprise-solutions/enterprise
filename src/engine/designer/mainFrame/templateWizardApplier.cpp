/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardApplier — implementation. See header.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizardApplier.h"

#include <wx/log.h>

#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

#include "backend/appData.h"
#include "backend/plugin/metaBridge.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/value_ptr.h"
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

static ibValueMetaObject* FindTopLevelObject(const std::string& fullName)
{
	if (activeMetaData == nullptr) return nullptr;
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr) return nullptr;
	const size_t dot = fullName.find('.');
	if (dot == std::string::npos) return nullptr;
	const std::string kind = NormalizedKind(fullName.substr(0, dot));
	const ibClassID clsid = kind == "catalog" ? g_metaCatalogCLSID :
	                        kind == "document" ? g_metaDocumentCLSID : 0;
	const wxString name = wxString::FromUTF8(fullName.substr(dot + 1).c_str());
	for (ibValueMetaObject* child : root->GetAnyArrayObject<>()) {
		if (child == nullptr || child->IsDeleted()) continue;
		if (child->GetName() != name) continue;
		if (clsid == 0 || child->GetClassType() == clsid) return child;
	}
	return nullptr;
}

static wxString CanonicalRowKey(const std::string& key)
{
	const std::string lower = LowerAscii(key);
	if (lower == "code")        return wxT("Code");
	if (lower == "description") return wxT("Description");
	if (lower == "number")      return wxT("Number");
	if (lower == "date")        return wxT("Date");
	if (lower == "period")      return wxT("Period");
	return wxString::FromUTF8(key.c_str());
}

static ibValue JsonToValue(const nlohmann::json& v, const wxString& key)
{
	if (v.is_boolean()) return ibValue(v.get<bool>());
	if (v.is_number_float()) return ibValue(v.get<double>());
	if (v.is_number_integer()) return ibValue(static_cast<double>(v.get<long long>()));
	if (v.is_number_unsigned()) return ibValue(static_cast<double>(v.get<unsigned long long>()));
	if (v.is_string()) {
		const std::string s = v.get<std::string>();
		const wxString ws = wxString::FromUTF8(s.c_str());
		const wxString lowerKey = key.Lower();
		if (lowerKey == wxT("date") || lowerKey == wxT("period") ||
		    lowerKey.Find(wxT("дата")) != wxNOT_FOUND) {
			wxDateTime dt;
			if (dt.ParseISODate(ws)) return ibValue(dt);
		}
		return ibValue(ws);
	}
	return ibValue();
}

static bool ApplyRowFields(ibValueRecordDataObjectRef* object,
                           const nlohmann::json& row,
                           wxString& error)
{
	if (object == nullptr || !row.is_object()) return false;
	const auto* meta = object->GetMetaObject();
	if (meta == nullptr) return false;
	for (auto& [rawKey, value] : row.items()) {
		if (value.is_array() || value.is_object()) continue;
		const wxString key = CanonicalRowKey(rawKey);
		ibValueMetaObjectAttributeBase* attr = nullptr;
		for (auto* candidate : meta->GetGenericAttributeArrayObject()) {
			if (candidate == nullptr || candidate->IsDeleted()) continue;
			wxString objectName;
			if (!candidate->GetObjectNameAsString(objectName)) continue;
			if (objectName.CmpNoCase(key) == 0) {
				attr = candidate;
				break;
			}
		}
		if (attr == nullptr) continue;
		if (!object->SetValueByMetaID(attr->GetMetaID(),
		                              JsonToValue(value, key))) {
			const bool requiredCore =
			    key.CmpNoCase(wxT("Code")) == 0 ||
			    key.CmpNoCase(wxT("Description")) == 0 ||
			    key.CmpNoCase(wxT("Number")) == 0 ||
			    key.CmpNoCase(wxT("Date")) == 0 ||
			    key.CmpNoCase(wxT("Period")) == 0;
			if (requiredCore) {
				error = wxString::Format(_("failed to set field '%s'"), key);
				return false;
			}
			wxLogWarning("Template Wizard: skipped demo-data field '%s' "
			             "because the raw JSON value does not match the "
			             "metadata attribute type yet.",
			             key);
		}
	}
	return true;
}

static bool InsertDataRow(const std::string& kind,
                          const std::string& fullName,
                          const nlohmann::json& row,
                          bool postAfterInsert,
                          wxString& error)
{
	(void)postAfterInsert; // posting needs reference resolution; insert visible rows first.
	ibValueMetaObject* meta = FindTopLevelObject(fullName);
	if (meta == nullptr) {
		error = _("metadata object not found");
		return false;
	}
	const std::string lowerKind = LowerAscii(kind.empty()
	    ? ibTemplateWizardPayload::InferKindFromFullName(fullName)
	    : kind);
	if (lowerKind == "catalog") {
		auto* catalog =
		    dynamic_cast<ibValueMetaObjectRecordDataHierarchyMutableRef*>(meta);
		if (catalog == nullptr) {
			error = _("target is not a catalog");
			return false;
		}
		ibValuePtr<ibValueRecordDataObjectHierarchyRef> object(
		    catalog->CreateObjectValue(ibObjectMode::OBJECT_ITEM));
		if (!object) {
			error = _("failed to create catalog row object");
			return false;
		}
		if (!ApplyRowFields(object, row, error)) return false;
		return object->WriteObject();
	}
	if (lowerKind == "document") {
		auto* document = dynamic_cast<ibValueMetaObjectDocument*>(meta);
		if (document == nullptr) {
			error = _("target is not a document");
			return false;
		}
		ibValuePtr<ibValueRecordDataObjectDocument> object(
		    document->CreateObjectValue());
		if (!object) {
			error = _("failed to create document row object");
			return false;
		}
		if (!ApplyRowFields(object, row, error)) return false;
		return object->WriteObject(
		    ibDocumentWriteMode::ibDocumentWriteMode_Write,
		    ibDocumentPostingMode::ibDocumentPostingMode_Regular);
	}
	error = _("data rows for this metadata kind are not supported yet");
	return false;
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

	if (includeData) {
		const int totalRows = CountDemoRows(*payload);
		if (totalRows > 0 && result.failureCount == 0) {
			if (activeMetaData == nullptr ||
			    !activeMetaData->SaveDatabase(saveConfigFlag)) {
				OpResult op;
				op.op = wxT("data-prepare");
				op.error = _("failed to save configuration before inserting demo data");
				result.ops.push_back(op);
				++result.failureCount;
				result.skippedDataRows += totalRows;
			} else {
				ibApplicationData::ScopedDesignerDataWrite writeScope;
				for (const auto& di : (*payload)["demoData"]) {
					if (!di.is_object()) continue;
					const std::string kind =
					    ibTemplateWizardPayload::StringField(di, "kind", "type");
					const std::string fullName =
					    ibTemplateWizardPayload::StringField(di, "fullName", "name", "path");
					const bool postAfterInsert =
					    di.value("postAfterInsert", false);
					if (!di.contains("rows") || !di["rows"].is_array()) continue;
					for (const auto& row : di["rows"]) {
						OpResult op;
						op.op = wxT("data-insert");
						op.kind = wxString::FromUTF8(kind.c_str());
						op.fullName = wxString::FromUTF8(fullName.c_str());
						wxString err;
						if (InsertDataRow(kind, fullName, row, postAfterInsert, err)) {
							op.success = true;
							++result.successCount;
							++result.insertedDataRows;
						} else {
							op.success = false;
							op.error = err;
							++result.failureCount;
							++result.skippedDataRows;
						}
						result.ops.push_back(op);
					}
				}
			}
		} else {
			result.skippedDataRows += totalRows;
		}
	}

	return result;
}

}  // namespace ibTemplateWizardApplier
