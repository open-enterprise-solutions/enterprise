/////////////////////////////////////////////////////////////////////////////
// ibTemplateWizardApplier — implementation. See header.
/////////////////////////////////////////////////////////////////////////////

#include "templateWizardApplier.h"

#include <wx/log.h>

#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/plugin/metaBridge.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaLanguageObject.h"
#include "backend/metaCollection/metaObjectMetadata.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/catalogManager.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/metaCollection/partial/informationRegister.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/objCtor.h"
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

static bool ReplaceWithJsonTextEnvelope(nlohmann::json& root)
{
	const nlohmann::json* holder = &root;
	if (root.contains("result") && root["result"].is_object()) {
		holder = &root["result"];
	}
	if (!holder->contains("content") || !(*holder)["content"].is_array()) {
		return false;
	}
	for (const auto& item : (*holder)["content"]) {
		if (!item.is_object() || !item.contains("text") ||
		    !item["text"].is_string()) {
			continue;
		}
		auto inner = nlohmann::json::parse(item["text"].get<std::string>(),
		                                   nullptr, false);
		if (!inner.is_discarded() && inner.is_object()) {
			root = std::move(inner);
			return true;
		}
	}
	return false;
}

static std::string LowerAscii(std::string s)
{
	for (char& ch : s) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}
	return s;
}

static bool ContainsCyrillicUtf8(const std::string& s)
{
	for (size_t i = 0; i + 1 < s.size(); ++i) {
		const unsigned char a = static_cast<unsigned char>(s[i]);
		const unsigned char b = static_cast<unsigned char>(s[i + 1]);
		if ((a == 0xD0 && b >= 0x80 && b <= 0xBF) ||
		    (a == 0xD1 && b >= 0x80 && b <= 0xBF)) {
			return true;
		}
	}
	return false;
}

static std::string TransliterateCyrillic(std::string s)
{
	static const std::pair<const char*, const char*> map[] = {
	    {"Щ","Shch"},{"щ","shch"},{"Ш","Sh"},{"ш","sh"},
	    {"Ч","Ch"},{"ч","ch"},{"Ц","Ts"},{"ц","ts"},
	    {"Ю","Yu"},{"ю","yu"},{"Я","Ya"},{"я","ya"},
	    {"Є","Ye"},{"є","ye"},{"Ї","Yi"},{"ї","yi"},
	    {"Ё","Yo"},{"ё","yo"},{"Ж","Zh"},{"ж","zh"},
	    {"Х","Kh"},{"х","kh"},{"Ґ","G"},{"ґ","g"},
	    {"А","A"},{"а","a"},{"Б","B"},{"б","b"},
	    {"В","V"},{"в","v"},{"Г","G"},{"г","g"},
	    {"Д","D"},{"д","d"},{"Е","E"},{"е","e"},
	    {"З","Z"},{"з","z"},{"И","I"},{"и","i"},
	    {"І","I"},{"і","i"},{"Й","Y"},{"й","y"},
	    {"К","K"},{"к","k"},{"Л","L"},{"л","l"},
	    {"М","M"},{"м","m"},{"Н","N"},{"н","n"},
	    {"О","O"},{"о","o"},{"П","P"},{"п","p"},
	    {"Р","R"},{"р","r"},{"С","S"},{"с","s"},
	    {"Т","T"},{"т","t"},{"У","U"},{"у","u"},
	    {"Ф","F"},{"ф","f"},{"Ы","Y"},{"ы","y"},
	    {"Э","E"},{"э","e"},{"Ь",""},{"ь",""},
	    {"Ъ",""},{"ъ",""},{"’",""},{"'",""},
	};
	for (const auto& item : map) {
		const std::string from(item.first);
		const std::string to(item.second);
		size_t pos = 0;
		while ((pos = s.find(from, pos)) != std::string::npos) {
			s.replace(pos, from.size(), to);
			pos += to.size();
		}
	}
	return s;
}

static bool IsDirectChildContainer(const std::string& segment)
{
	const std::string lower = LowerAscii(segment);
	return lower == "forms" || lower == "form" ||
	       lower == "attributes" || lower == "attribute" ||
	       lower == "dimensions" || lower == "dimension" ||
	       lower == "resources" || lower == "resource" ||
	       lower == "values" || lower == "value" ||
	       lower == "commands" || lower == "command" ||
	       lower == "templates" || lower == "template" ||
	       lower == "printforms" || lower == "printform" ||
	       lower == "tabularsections" || lower == "tabularsection";
}

static std::vector<std::string> SplitDottedPath(const std::string& s)
{
	std::vector<std::string> parts;
	size_t start = 0;
	while (start <= s.size()) {
		const size_t dot = s.find('.', start);
		if (dot == std::string::npos) {
			parts.push_back(s.substr(start));
			break;
		}
		parts.push_back(s.substr(start, dot - start));
		start = dot + 1;
	}
	return parts;
}

static std::string LeafNameFromPossiblyQualifiedName(const std::string& name)
{
	const auto parts = SplitDottedPath(name);
	return parts.empty() ? name : parts.back();
}

static std::string LooseAsciiKey(const std::string& value)
{
	const std::string source = ContainsCyrillicUtf8(value)
	    ? TransliterateCyrillic(value)
	    : value;
	std::string out;
	for (char ch : LowerAscii(source)) {
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
			out.push_back(ch);
		}
	}
	return out;
}

static bool IsDefaultFormTypeTokenLocal(const std::string& value)
{
	const std::string key = LooseAsciiKey(value);
	return key.empty() || key == "form" || key == "default" ||
	       key == "defaultform";
}

static std::string InferFormTypeToken(const std::string& formName,
                                      const std::string& ownerFullName)
{
	const std::string key = LooseAsciiKey(formName);
	const std::string ownerKind = SplitDottedPath(ownerFullName).empty()
	    ? std::string()
	    : LowerAscii(SplitDottedPath(ownerFullName).front());

	if (key.find("folderselect") != std::string::npos ||
	    key.find("groupselect") != std::string::npos ||
	    key.find("vyboragrupp") != std::string::npos) {
		return "FormGroupSelect";
	}
	if (key.find("folder") != std::string::npos ||
	    key.find("group") != std::string::npos ||
	    key.find("grupp") != std::string::npos) {
		return "FormFolder";
	}
	if (key.find("list") != std::string::npos ||
	    key.find("spisk") != std::string::npos) {
		return "FormList";
	}
	if (key.find("select") != std::string::npos ||
	    key.find("choice") != std::string::npos ||
	    key.find("vybor") != std::string::npos) {
		return "FormSelect";
	}
	if (key.find("object") != std::string::npos ||
	    key.find("item") != std::string::npos ||
	    key.find("element") != std::string::npos ||
	    key.find("dokument") != std::string::npos ||
	    key.find("document") != std::string::npos) {
		return "FormObject";
	}
	if (ownerKind == "document" || ownerKind == "catalog" ||
	    ownerKind == "dataprocessor" || ownerKind == "report") {
		return "FormObject";
	}
	if (ownerKind == "informationregister" ||
	    ownerKind == "accumulationregister" ||
	    ownerKind == "accountingregister") {
		return "FormList";
	}
	return "FormObject";
}

static std::string NormalizeTechnicalNameSegment(const std::string& segment)
{
	if (segment.empty() || IsDirectChildContainer(segment)) return segment;
	std::string s = ContainsCyrillicUtf8(segment)
	    ? TransliterateCyrillic(segment)
	    : segment;
	std::string out;
	out.reserve(s.size());
	bool upperNext = false;
	for (unsigned char ch : s) {
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
		    (ch >= '0' && ch <= '9') || ch == '_') {
			out.push_back(upperNext && ch >= 'a' && ch <= 'z'
			    ? static_cast<char>(std::toupper(ch))
			    : static_cast<char>(ch));
			upperNext = false;
		} else {
			upperNext = !out.empty();
		}
	}
	if (out.empty()) out = "Object";
	if (out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
	return out;
}

static std::string NormalizePathKindSegment(const std::string& segment)
{
	const std::string lower = LowerAscii(segment);
	if (lower == "subsystem") return "Interface";
	if (lower == "enum") return "Enumeration";
	if (lower == "inforegister") return "InformationRegister";
	return NormalizeTechnicalNameSegment(segment);
}

static std::string NormalizeTechnicalFullName(const std::string& fullName)
{
	const auto parts = SplitDottedPath(fullName);
	if (parts.empty()) return fullName;
	std::string out;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i > 0) out += ".";
		out += (i == 0)
		    ? NormalizePathKindSegment(parts[i])
		    : NormalizeTechnicalNameSegment(parts[i]);
	}
	return out;
}

static std::string NormalizeTypeName(const std::string& typeName)
{
	const auto parts = SplitDottedPath(typeName);
	if (parts.size() != 2) return typeName;
	const std::string lowerKind = LowerAscii(parts[0]);
	if (lowerKind != "catalogref" && lowerKind != "documentref" &&
	    lowerKind != "enumref" &&
	    lowerKind != "enumerationref" &&
	    lowerKind != "chartofaccountsref" &&
	    lowerKind != "chartofcharacteristictypesref" &&
	    lowerKind != "catalog" && lowerKind != "document" &&
	    lowerKind != "enum" &&
	    lowerKind != "enumeration" &&
	    lowerKind != "chartofaccounts" &&
	    lowerKind != "chartofcharacteristictypes") {
		return typeName;
	}
	std::string kind = parts[0];
	if (lowerKind == "enum") kind = "Enumeration";
	else if (lowerKind == "enumref") kind = "EnumerationRef";
	return kind + "." + NormalizeTechnicalNameSegment(parts[1]);
}

static void NormalizePropertyReferenceType(nlohmann::json& obj)
{
	if (!obj.is_object()) return;
	nlohmann::json* type = nullptr;
	if (obj.contains("type") && obj["type"].is_string()) {
		type = &obj["type"];
	} else if (obj.contains("valueType") && obj["valueType"].is_string()) {
		type = &obj["valueType"];
	}
	if (type == nullptr) return;
	const std::string rawType = type->get<std::string>();
	const std::string lowerType = LowerAscii(rawType);
	auto setRef = [&](const char* field, const char* refKind) {
		if (!obj.contains(field) || !obj[field].is_string()) return false;
		const std::string target = obj[field].get<std::string>();
		if (target.empty()) return false;
		*type = std::string(refKind) + "." + NormalizeTechnicalNameSegment(target);
		return true;
	};
	if (lowerType == "catalogref" || lowerType == "catalog") {
		(void)setRef("catalog", "CatalogRef");
	} else if (lowerType == "documentref" || lowerType == "document") {
		(void)setRef("document", "DocumentRef");
	} else if (lowerType == "enum" || lowerType == "enumeration" ||
	           lowerType == "enumref" || lowerType == "enumerationref") {
		if (!setRef("enum", "EnumerationRef")) {
			(void)setRef("enumeration", "EnumerationRef");
		}
	} else if (lowerType == "chartofaccountsref" ||
	           lowerType == "chartofaccounts") {
		(void)setRef("chartOfAccounts", "ChartOfAccountsRef");
	} else if (lowerType == "chartofcharacteristictypesref" ||
	           lowerType == "chartofcharacteristictypes") {
		(void)setRef("chartOfCharacteristicTypes",
		             "ChartOfCharacteristicTypesRef");
	}
}

static void NormalizePropertyTypes(nlohmann::json& value)
{
	if (value.is_object()) {
		NormalizePropertyReferenceType(value);
		for (auto& kv : value.items()) {
			if ((kv.key() == "type" || kv.key() == "valueType") &&
			    kv.value().is_string()) {
				kv.value() = NormalizeTypeName(kv.value().get<std::string>());
			} else {
				NormalizePropertyTypes(kv.value());
			}
		}
	} else if (value.is_array()) {
		for (auto& item : value) NormalizePropertyTypes(item);
	}
}

static std::string NormalizedKind(std::string kind)
{
	const std::string lower = LowerAscii(kind);
	if (lower == "enum") return "Enumeration";
	if (lower == "commonmodule") return "CommonModule";
	if (lower == "commonform") return "CommonForm";
	if (lower == "commontemplate") return "CommonTemplate";
	if (lower == "subsystem") return "Interface";
	if (lower == "inforegister") return "InformationRegister";
	if (lower == "enumvalue" || lower == "enumerationvalue") return "EnumValue";
	if (lower == "dimension") return "Dimension";
	if (lower == "resource") return "Resource";
	if (kind.empty()) return kind;
	kind[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(kind[0])));
	return kind;
}

static bool IsAlreadyExistsError(const wxString& detail)
{
	const std::string lower = LowerAscii(std::string(detail.utf8_str()));
	return lower.find("already exists") != std::string::npos ||
	       lower.find("child already exists") != std::string::npos ||
	       lower.find("object already exists") != std::string::npos;
}

static std::string OwnerFullNameFromProperties(const nlohmann::json& props)
{
	std::string owner = ibTemplateWizardPayload::StringField(
	    props, "owner", "ownerFullName", "parent");
	if (owner.empty() && props.contains("metadataObject") &&
	    props["metadataObject"].is_string()) {
		owner = props["metadataObject"].get<std::string>();
	}
	return owner.empty() ? owner : NormalizeTechnicalFullName(owner);
}

static std::string ModuleTargetFromOwner(const std::string& owner,
                                         const nlohmann::json& props)
{
	if (owner.empty()) return std::string();
	std::string moduleKind = ibTemplateWizardPayload::StringField(
	    props, "moduleKind", "moduleType", "kind");
	moduleKind = NormalizedKind(moduleKind);
	const std::string lowerModuleKind = LowerAscii(moduleKind);
	const std::string lowerOwner = LowerAscii(owner);

	// Form modules are edited on the form object itself; metaBridge stores
	// moduleCode on Form paths rather than on a separate ".Module" child.
	if (lowerModuleKind == "formmodule" ||
	    lowerOwner.find(".forms.") != std::string::npos) {
		return owner;
	}
	if (lowerModuleKind == "managermodule" || lowerModuleKind == "manager") {
		return owner + ".ManagerModule";
	}
	return owner + ".ObjectModule";
}

static std::string NormalizedOp(const nlohmann::json& m)
{
	std::string op = ibTemplateWizardPayload::StringField(
	    m, "op", "operation", "action");
	if (op.empty()) return "create";
	op = LowerAscii(op);
	if (op == "meta.create" || op == "createobject" || op == "add") return "create";
	if (op == "meta.edit"   || op == "update")       return "edit";
	if (op == "meta.delete" || op == "remove")       return "delete";
	if (op == "insert") return "data-insert";
	return op;
}

static bool IsKnownTopLevelKind(const std::string& kind)
{
	const std::string lower = LowerAscii(kind);
	return lower == "constant" || lower == "catalog" ||
	       lower == "document" || lower == "enumeration" ||
	       lower == "commonmodule" || lower == "commonform" ||
	       lower == "commontemplate" || lower == "template" ||
	       lower == "printform" || lower == "picture" ||
	       lower == "interface" || lower == "role" ||
	       lower == "language" || lower == "dataprocessor" ||
	       lower == "report" || lower == "informationregister" ||
	       lower == "accumulationregister" ||
	       lower == "chartofcharacteristictypes" ||
	       lower == "chartofaccounts" ||
	       lower == "accountingregister";
}

static bool IsKnownChildKind(const std::string& kind)
{
	const std::string lower = LowerAscii(kind);
	return lower == "form" || lower == "itemform" ||
	       lower == "listform" || lower == "choiceform" ||
	       lower == "selectionform" || lower == "attribute" ||
	       lower == "dimension" || lower == "resource" ||
	       lower == "enumvalue" ||
	       lower == "tabularsection" ||
	       lower == "tabularsectionattribute" ||
	       lower == "command" || lower == "objectmodule" ||
	       lower == "managermodule";
}

static bool IsConfigurationMutation(const std::string& kind,
                                    const std::string& fullName)
{
	return LowerAscii(kind) == "configuration" ||
	       LowerAscii(ibTemplateWizardPayload::InferKindFromFullName(fullName)) ==
	           "configuration";
}

static std::string MutationFullName(const nlohmann::json& m)
{
	return NormalizeTechnicalFullName(
	    ibTemplateWizardPayload::StringField(m, "fullName", "name", "path"));
}

static std::string MutationKind(const nlohmann::json& m,
                                const std::string& fullName)
{
	std::string kind = ibTemplateWizardPayload::StringField(m, "kind", "type");
	if (kind.empty()) {
		kind = ibTemplateWizardPayload::InferKindFromFullName(fullName);
	}
	return NormalizedKind(kind);
}

static int MutationSortRank(const nlohmann::json& m)
{
	const std::string op = NormalizedOp(m);
	const std::string fullName = MutationFullName(m);
	const std::string kind = MutationKind(m, fullName);
	if (IsConfigurationMutation(kind, fullName)) return 0;
	if (op == "delete") return 900;
	if (op == "edit") {
		const std::string lowerName = LowerAscii(fullName);
		if (lowerName.find(".objectmodule") != std::string::npos ||
		    lowerName.find(".managermodule") != std::string::npos ||
		    lowerName.find(".forms.") != std::string::npos) {
			return 800;
		}
		return 700;
	}
	const auto parts = SplitDottedPath(fullName);
	if (parts.size() <= 2) {
		const std::string lowerKind = LowerAscii(kind);
		if (lowerKind == "interface" || lowerKind == "role" ||
		    lowerKind == "language") return 10;
		if (lowerKind == "commonmodule") return 20;
		if (lowerKind == "enumeration") return 30;
		if (lowerKind == "catalog" ||
		    lowerKind == "chartofcharacteristictypes" ||
		    lowerKind == "chartofaccounts") return 40;
		if (lowerKind == "informationregister" ||
		    lowerKind == "accumulationregister" ||
		    lowerKind == "accountingregister") return 50;
		if (lowerKind == "document") return 60;
		if (lowerKind == "report" || lowerKind == "dataprocessor") return 70;
		return 80;
	}
	const std::string lowerName = LowerAscii(fullName);
	if (lowerName.find(".attributes.") != std::string::npos ||
	    lowerName.find(".dimensions.") != std::string::npos ||
	    lowerName.find(".resources.") != std::string::npos) return 200;
	if (lowerName.find(".values.") != std::string::npos) return 210;
	if (lowerName.find(".tabularsections.") != std::string::npos) return 220;
	if (lowerName.find(".templates.") != std::string::npos) return 240;
	if (lowerName.find(".forms.") != std::string::npos) return 260;
	if (lowerName.find(".commands.") != std::string::npos) return 280;
	return 300;
}

static void SortMutationsByDependency(std::vector<nlohmann::json>& expanded)
{
	std::stable_sort(expanded.begin(), expanded.end(),
	                 [](const nlohmann::json& a, const nlohmann::json& b) {
		                 return MutationSortRank(a) < MutationSortRank(b);
	                 });
}

static bool HostObjectExists(const std::string& fullName)
{
	if (fullName.empty()) return false;
	char* jsonOut = nullptr;
	char* err = nullptr;
	const int rc = metaBridge::HostMetaQuery(fullName.c_str(), nullptr,
	                                         &jsonOut, &err);
	if (jsonOut != nullptr) std::free(jsonOut);
	if (err != nullptr) std::free(err);
	return rc == 0;
}

static wxString RestructureDiagnostics()
{
	wxString out;
	for (const auto& entry : s_restructureInfo) {
		if (!out.IsEmpty()) out += wxT("; ");
		switch (entry.type) {
		case ibRestructure::error:   out += wxT("error: "); break;
		case ibRestructure::warning: out += wxT("warning: "); break;
		default:                     out += wxT("info: "); break;
		}
		out += entry.descr;
	}
	return out;
}

static ibValueMetaObject* FindTopLevelByPath(const std::string& fullName)
{
	if (activeMetaData == nullptr) return nullptr;
	const auto parts = SplitDottedPath(NormalizeTechnicalFullName(fullName));
	if (parts.size() != 2) return nullptr;
	const unsigned long long clsid = metaBridge::KindStringToCLSID(parts[0].c_str());
	if (clsid == 0) return nullptr;
	return activeMetaData->FindAnyObjectByFilter(
	    wxString::FromUTF8(parts[1].c_str()),
	    static_cast<ibClassID>(clsid));
}

static ibClassID ChildContainerCLSIDLocal(const std::string& container)
{
	const std::string c = LowerAscii(container);
	if (c == "forms" || c == "form") return g_metaFormCLSID;
	if (c == "attributes" || c == "attribute") return g_metaAttributeCLSID;
	if (c == "dimensions" || c == "dimension") return g_metaDimensionCLSID;
	if (c == "resources" || c == "resource") return g_metaResourceCLSID;
	if (c == "values" || c == "value" ||
	    c == "enumvalues" || c == "enumvalue" ||
	    c == "enumerationvalues" || c == "enumerationvalue") return g_metaEnumCLSID;
	if (c == "tabularsections" || c == "tabularsection" ||
	    c == "tables" || c == "table") return g_metaTableCLSID;
	if (c == "templates" || c == "template" ||
	    c == "printforms" || c == "printform") return g_metaTemplateCLSID;
	return 0;
}

static ibValueMetaObject* FindDirectChildByName(ibValueMetaObject* parent,
                                                ibClassID clsid,
                                                const std::string& name)
{
	if (parent == nullptr || clsid == 0) return nullptr;
	const wxString needle = wxString::FromUTF8(name.c_str());
	for (ibValueMetaObject* child : parent->GetAnyArrayObject<>()) {
		if (child == nullptr || child->IsDeleted()) continue;
		if (child->GetClassType() != clsid) continue;
		if (child->GetName() == needle) return child;
	}
	return nullptr;
}

static ibValueMetaObject* FindMetaObjectByFullName(const std::string& fullName)
{
	const auto parts = SplitDottedPath(NormalizeTechnicalFullName(fullName));
	if (parts.size() == 2) return FindTopLevelByPath(fullName);
	if (parts.size() != 4 && parts.size() != 6) return nullptr;
	ibValueMetaObject* top = FindTopLevelByPath(parts[0] + "." + parts[1]);
	if (top == nullptr) return nullptr;
	ibValueMetaObject* child = FindDirectChildByName(
	    top, ChildContainerCLSIDLocal(parts[2]), parts[3]);
	if (parts.size() == 4) return child;
	return FindDirectChildByName(child, ChildContainerCLSIDLocal(parts[4]),
	                             parts[5]);
}

static int FormTypeIDFromToken(const std::string& value)
{
	const std::string key = LooseAsciiKey(value);
	if (key == "formobject" || key == "objectform" ||
	    key == "itemform" || key == "formelementa" ||
	    key == "elementform" || key == "formadokumenta" ||
	    key == "documentform" || key == "formdocument") return 1;
	if (key == "formlist" || key == "listform" ||
	    key == "formaspisku") return 2;
	if (key == "formselect" || key == "selectform" ||
	    key == "choiceform" || key == "selectionform" ||
	    key == "formavybora") return 3;
	if (key == "formfolder" || key == "folderform" ||
	    key == "formagruppy") return 4;
	if (key == "formgroupselect" || key == "folderselectform" ||
	    key == "formavyboragruppy") return 5;
	return defaultFormType;
}

static bool ForceFormType(ibValueMetaObjectFormBase* form,
                          const std::string& formTypeToken,
                          wxString& errOut)
{
	if (form == nullptr) return true;
	const int formType = FormTypeIDFromToken(formTypeToken);
	if (formType == defaultFormType) {
		errOut = wxString::Format(_("unknown form type '%s'"),
		                          wxString::FromUTF8(formTypeToken.c_str()));
		return false;
	}
	ibProperty* prop = form->GetProperty(wxT("FormType"));
	if (prop == nullptr) return true;
	prop->SetValue(wxVariant(static_cast<long>(formType)));
	if (form->GetTypeForm() != formType) {
		errOut = _("form type was rejected by metadata object");
		return false;
	}
	return true;
}

static bool EnsureDefaultLanguage(wxString& errOut)
{
	if (activeMetaData == nullptr) {
		errOut = _("no active metadata");
		return false;
	}
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	auto* config = dynamic_cast<ibValueMetaObjectConfiguration*>(root);
	if (config == nullptr) return true;

	auto languages =
	    activeMetaData->GetAnyArrayObject<ibValueMetaObjectLanguage>(
	        g_metaLanguageCLSID);
	if (languages.empty()) {
		ibValueMetaObject* created =
		    activeMetaData->CreateMetaObject(g_metaLanguageCLSID, root, true);
		auto* lang = dynamic_cast<ibValueMetaObjectLanguage*>(created);
		if (lang == nullptr) {
			errOut = _("failed to create default language metadata object");
			return false;
		}
		(void)activeMetaData->RenameMetaObject(lang, wxT("English"));
		lang->SetName(wxT("English"));
		lang->SetSynonym(wxT("English"));
		lang->SetLangCode(wxT("en"));
		languages.push_back(lang);
	}

	if (activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectLanguage>(
	        config->GetLanguage(), g_metaLanguageCLSID) == nullptr &&
	    !languages.empty()) {
		config->SetLanguage(languages.front()->GetMetaID());
	}
	return true;
}

static ibValueMetaObject* PickRecorderDocument(
    const std::vector<nlohmann::json>& expanded)
{
	std::set<std::string> plannedDocuments;
	for (const auto& m : expanded) {
		if (!m.is_object() || NormalizedOp(m) != "create") continue;
		const std::string fullName = NormalizeTechnicalFullName(MutationFullName(m));
		const auto parts = SplitDottedPath(fullName);
		if (parts.size() == 2 &&
		    LowerAscii(MutationKind(m, fullName)) == "document") {
			plannedDocuments.insert(fullName);
		}
	}
	for (const auto& fullName : plannedDocuments) {
		if (ibValueMetaObject* doc = FindTopLevelByPath(fullName)) {
			return doc;
		}
	}
	if (activeMetaData == nullptr) return nullptr;
	const auto docs =
	    activeMetaData->GetAnyArrayObject<ibValueMetaObjectDocument>(
	        g_metaDocumentCLSID);
	return docs.empty() ? nullptr : docs.front();
}

static bool EnsureRegisterRecorders(const std::vector<nlohmann::json>& expanded,
                                    wxString& errOut)
{
	if (activeMetaData == nullptr) return true;
	ibValueMetaObject* recorderDoc = nullptr;
	ibClassID recorderType = 0;
	ibTypeDescription::ibTypeData typeData;

	auto ensureRecorderType = [&]() -> bool {
		if (recorderType != 0) return true;
		recorderDoc = PickRecorderDocument(expanded);
		if (recorderDoc == nullptr) {
			errOut = _("registers require a recorder document, but no Document metadata exists");
			return false;
		}
		const ibCtorMetaValueType* ctor =
		    activeMetaData->GetTypeCtor(recorderDoc,
		                                ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
		if (ctor == nullptr) {
			errOut = _("failed to resolve recorder document reference type");
			return false;
		}
		recorderType = ctor->GetClassType();
		return true;
	};

	for (ibValueMetaObjectRegisterData* reg :
	     activeMetaData->GetAnyArrayObject<ibValueMetaObjectRegisterData>(
	         { g_metaInformationRegisterCLSID,
	           g_metaAccumulationRegisterCLSID,
	           g_metaAccountingRegisterCLSID })) {
		if (reg == nullptr || !reg->HasRecorder()) continue;
		ibValueMetaObjectAttributePredefined* recorder =
		    reg->GetRegisterRecorder();
		if (recorder == nullptr) continue;
		if (!ensureRecorderType()) return false;
		recorder->GetTypeDesc().SetDefaultMetaType(recorderType, typeData);
	}
	return true;
}

static bool PrepareConfigurationForDemoData(
    const std::vector<nlohmann::json>& expanded,
    wxString& errOut)
{
	if (activeMetaData == nullptr) {
		errOut = _("no active metadata");
		return false;
	}
	wxString repairError;
	if (!EnsureDefaultLanguage(repairError) ||
	    !EnsureRegisterRecorders(expanded, repairError)) {
		errOut = repairError;
		return false;
	}

	if (!activeMetaData->SaveDatabase()) {
		errOut = _("failed to save configuration workspace before demo data");
		const wxString detail = RestructureDiagnostics();
		if (!detail.IsEmpty()) errOut += wxT(": ") + detail;
		return false;
	}
	if (!activeMetaData->SaveDatabase(saveConfigFlag)) {
		errOut = _("failed to update database schema before demo data");
		const wxString detail = RestructureDiagnostics();
		if (!detail.IsEmpty()) errOut += wxT(": ") + detail;
		return false;
	}
	return true;
}

static void AddDiagnostic(ApplyResult& result,
                          const char* opName,
                          const std::string& kind,
                          const std::string& fullName,
                          const wxString& error,
                          bool warningOnly = false)
{
	OpResult op;
	op.op = wxString::FromUTF8(opName);
	op.kind = wxString::FromUTF8(kind.c_str());
	op.fullName = wxString::FromUTF8(fullName.c_str());
	op.success = warningOnly;
	op.error = error;
	result.ops.push_back(op);
	if (warningOnly) {
		++result.completenessWarningCount;
	} else {
		++result.failureCount;
		++result.preflightFailureCount;
	}
}

static int CountDemoRows(const nlohmann::json& payload);

static std::string TopLevelPath(const std::string& fullName)
{
	const auto parts = SplitDottedPath(fullName);
	if (parts.size() < 2) return std::string();
	return parts[0] + "." + parts[1];
}

static void CollectReferenceTypes(const nlohmann::json& value,
                                  std::vector<std::string>& out)
{
	if (value.is_object()) {
		for (auto& kv : value.items()) {
			if ((kv.key() == "type" || kv.key() == "valueType") &&
			    kv.value().is_string()) {
				const std::string typeName =
				    NormalizeTypeName(kv.value().get<std::string>());
				const auto parts = SplitDottedPath(typeName);
				if (parts.size() == 2) {
					const std::string lowerKind = LowerAscii(parts[0]);
					if (lowerKind == "catalogref" ||
					    lowerKind == "documentref" ||
					    lowerKind == "enumerationref" ||
					    lowerKind == "chartofaccountsref" ||
					    lowerKind == "chartofcharacteristictypesref") {
						out.push_back(typeName);
					}
				}
			} else {
				CollectReferenceTypes(kv.value(), out);
			}
		}
	} else if (value.is_array()) {
		for (const auto& item : value) CollectReferenceTypes(item, out);
	}
}

static std::string RefTypeToTopLevelPath(const std::string& typeName)
{
	const auto parts = SplitDottedPath(typeName);
	if (parts.size() != 2) return std::string();
	const std::string lowerKind = LowerAscii(parts[0]);
	std::string kind;
	if (lowerKind == "catalogref") kind = "Catalog";
	else if (lowerKind == "documentref") kind = "Document";
	else if (lowerKind == "enumerationref") kind = "Enumeration";
	else if (lowerKind == "chartofaccountsref") kind = "ChartOfAccounts";
	else if (lowerKind == "chartofcharacteristictypesref") {
		kind = "ChartOfCharacteristicTypes";
	}
	if (kind.empty()) return std::string();
	return kind + "." + NormalizeTechnicalNameSegment(parts[1]);
}

static void PreflightValidateMutations(const std::vector<nlohmann::json>& expanded,
                                       ApplyResult& result)
{
	std::set<std::string> expectedTopLevel;
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		const std::string opStr = NormalizedOp(m);
		if (opStr != "create" && opStr != "edit") continue;
		const std::string fullName = NormalizeTechnicalFullName(MutationFullName(m));
		const std::string kind = MutationKind(m, fullName);
		if (IsConfigurationMutation(kind, fullName)) continue;
		const auto parts = SplitDottedPath(fullName);
		if (parts.size() == 2 && opStr == "create") {
			expectedTopLevel.insert(fullName);
		}
	}

	for (const auto& m : expanded) {
		if (!m.is_object()) {
			AddDiagnostic(result, "preflight", "", "",
			              _("mutation entry is not a JSON object"));
			continue;
		}
		const std::string opStr = NormalizedOp(m);
		const std::string fullName = NormalizeTechnicalFullName(MutationFullName(m));
		const std::string kind = MutationKind(m, fullName);
		const auto parts = SplitDottedPath(fullName);
		if (opStr != "create" && opStr != "edit" && opStr != "delete") {
			AddDiagnostic(result, "preflight", kind, fullName,
			              wxString::Format(_("unsupported mutation op '%s'"),
			                               wxString::FromUTF8(opStr.c_str())));
			continue;
		}
		if (fullName.empty() || parts.size() < 2) {
			AddDiagnostic(result, "preflight", kind, fullName,
			              _("metadata fullName must be a dotted path"));
			continue;
		}
		if (!IsConfigurationMutation(kind, fullName)) {
			if (parts.size() == 2 && !IsKnownTopLevelKind(kind)) {
				AddDiagnostic(result, "preflight", kind, fullName,
				              _("unsupported top-level metadata kind"));
				continue;
			}
			if (parts.size() > 2 && !IsKnownChildKind(kind)) {
				AddDiagnostic(result, "preflight", kind, fullName,
				              _("unsupported child metadata kind"));
				continue;
			}
			const std::string parent = TopLevelPath(fullName);
			if (parts.size() > 2 &&
			    expectedTopLevel.find(parent) == expectedTopLevel.end() &&
			    !HostObjectExists(parent)) {
				AddDiagnostic(result, "preflight", kind, fullName,
				              wxString::Format(
				                  _("parent metadata object '%s' is not planned and does not exist"),
				                  wxString::FromUTF8(parent.c_str())));
				continue;
			}
		}

		const nlohmann::json* propsObj = nullptr;
		if (m.contains("properties")) propsObj = &m["properties"];
		else if (m.contains("props")) propsObj = &m["props"];
		else if (m.contains("definition")) propsObj = &m["definition"];
		nlohmann::json propsJson = propsObj != nullptr
		    ? *propsObj
		    : nlohmann::json::object();
		NormalizePropertyTypes(propsJson);
		std::vector<std::string> refs;
		CollectReferenceTypes(propsJson, refs);
		for (const auto& refType : refs) {
			const std::string refPath = RefTypeToTopLevelPath(refType);
			if (refPath.empty()) continue;
			if (expectedTopLevel.find(refPath) == expectedTopLevel.end() &&
			    !HostObjectExists(refPath)) {
				AddDiagnostic(result, "preflight", kind, fullName,
				              wxString::Format(
				                  _("reference type '%s' points to missing metadata '%s'"),
				                  wxString::FromUTF8(refType.c_str()),
				                  wxString::FromUTF8(refPath.c_str())));
			}
		}
	}
}

static void EvaluateCompleteness(const std::vector<nlohmann::json>& expanded,
                                 const nlohmann::json& payload,
                                 bool includeData,
                                 ApplyResult& result)
{
	std::map<std::string, int> topKinds;
	int childForms = 0;
	int childCommands = 0;
	int moduleEdits = 0;
	int tabularSections = 0;
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		const std::string opStr = NormalizedOp(m);
		const std::string fullName = MutationFullName(m);
		const std::string kind = MutationKind(m, fullName);
		if (IsConfigurationMutation(kind, fullName)) continue;
		const auto parts = SplitDottedPath(fullName);
		if (parts.size() == 2 && opStr == "create") {
			++topKinds[LowerAscii(kind)];
		}
		const std::string lowerName = LowerAscii(fullName);
		if (lowerName.find(".forms.") != std::string::npos) ++childForms;
		if (lowerName.find(".commands.") != std::string::npos) ++childCommands;
		if (lowerName.find(".objectmodule") != std::string::npos ||
		    lowerName.find(".managermodule") != std::string::npos) ++moduleEdits;
		if (lowerName.find(".tabularsections.") != std::string::npos) {
			++tabularSections;
		}
	}

	int score = 0;
	auto add = [&](bool ok, int points, const wxString& warning) {
		if (ok) {
			score += points;
		} else {
			AddDiagnostic(result, "completeness", "Configuration",
			              "Configuration", warning, /*warningOnly=*/true);
		}
	};
	const bool hasCatalog = topKinds["catalog"] > 0;
	const bool hasWorkflowObject =
	    topKinds["document"] > 0 || topKinds["dataprocessor"] > 0;
	const bool hasState =
	    topKinds["enumeration"] > 0 ||
	    topKinds["informationregister"] > 0 ||
	    topKinds["accumulationregister"] > 0 ||
	    topKinds["accountingregister"] > 0;
	const bool hasRegister =
	    topKinds["informationregister"] > 0 ||
	    topKinds["accumulationregister"] > 0 ||
	    topKinds["accountingregister"] > 0;
	const bool hasReportOrProcessor =
	    topKinds["report"] > 0 || topKinds["dataprocessor"] > 0;
	const bool hasRoleOrInterface =
	    topKinds["role"] > 0 || topKinds["interface"] > 0;

	add(hasCatalog, 15, _("complete business configuration has no master-data catalogs"));
	add(hasWorkflowObject, 15, _("complete business configuration has no documents/process objects"));
	add(hasState, 15, _("complete business configuration has no lifecycle/status/state metadata"));
	add(hasRegister, 15, _("complete business configuration has no registers for state/totals"));
	add(childForms > 0, 10, _("complete business configuration has no generated forms"));
	add(moduleEdits > 0, 10, _("complete business configuration has no module/business logic edits"));
	add(hasReportOrProcessor, 10, _("complete business configuration has no reports/process workbench"));
	add(hasRoleOrInterface, 5, _("complete business configuration has no roles/interfaces"));
	add(!includeData || CountDemoRows(payload) > 0, 5,
	    _("complete business configuration has no demo data"));
	if (childCommands > 0) score += 3;
	if (tabularSections > 0) score += 2;
	result.completenessScore = std::min(score, 100);
	if (result.completenessScore < 70) {
		AddDiagnostic(result, "preflight", "Configuration", "Configuration",
		              wxString::Format(
		                  _("business configuration completeness score is too low for production apply: %d/100"),
		                  result.completenessScore));
	}
}

static std::set<std::string> ExpectedTopLevelObjects(
    const std::vector<nlohmann::json>& expanded)
{
	std::set<std::string> expected;
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		if (NormalizedOp(m) != "create") continue;
			const std::string fullName = NormalizeTechnicalFullName(MutationFullName(m));
			if (SplitDottedPath(fullName).size() == 2) {
				expected.insert(fullName);
			}
	}
	return expected;
}

static std::string LooseMetadataKey(const std::string& value)
{
	std::string out;
	for (char ch : LowerAscii(NormalizeTechnicalFullName(value))) {
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
			out.push_back(ch);
		}
	}
	return out;
}

static std::string ResolveDemoDataTarget(
    const std::string& requestedFullName,
    const std::set<std::string>& expectedTopLevel)
{
	const std::string requested = NormalizeTechnicalFullName(requestedFullName);
	if (requested.empty()) return requested;
	if (expectedTopLevel.find(requested) != expectedTopLevel.end() ||
	    HostObjectExists(requested)) {
		return requested;
	}

	const auto parts = SplitDottedPath(requested);
	if (parts.size() != 2) return requested;
	const std::string requestedKind = parts[0];
	const std::string requestedLeafKey = LooseMetadataKey(parts[1]);
	if (requestedLeafKey.empty()) return requested;

	std::string matched;
	for (const auto& candidate : expectedTopLevel) {
		const auto candidateParts = SplitDottedPath(candidate);
		if (candidateParts.size() != 2) continue;
		if (LowerAscii(candidateParts[0]) != LowerAscii(requestedKind)) continue;
		const std::string candidateLeafKey = LooseMetadataKey(candidateParts[1]);
		const bool prefixMatch =
		    candidateLeafKey.find(requestedLeafKey) == 0 ||
		    requestedLeafKey.find(candidateLeafKey) == 0;
		if (!prefixMatch) continue;
		if (!matched.empty()) return requested; // ambiguous: keep original and report.
		matched = candidate;
	}
	return matched.empty() ? requested : matched;
}

static std::string DemoDataFullName(const nlohmann::json& di)
{
	return NormalizeTechnicalFullName(
	    ibTemplateWizardPayload::StringField(di, "fullName", "name", "path"));
}

static std::string DemoDataRef(const nlohmann::json& di)
{
	std::string ref = ibTemplateWizardPayload::StringField(di, "ref", "target", "object");
	if (ref.empty()) ref = DemoDataFullName(di);
	return NormalizeTechnicalFullName(ref);
}

static std::string DemoDataKind(const nlohmann::json& di,
                                const std::string& fullName)
{
	std::string kind = NormalizedKind(
	    ibTemplateWizardPayload::StringField(di, "kind", "type"));
	if (kind.empty()) {
		kind = NormalizedKind(
		    ibTemplateWizardPayload::InferKindFromFullName(fullName));
	}
	return kind;
}

static const nlohmann::json* DemoDataRows(const nlohmann::json& di)
{
	if (di.contains("rows") && di["rows"].is_array()) return &di["rows"];
	if (di.contains("data") && di["data"].is_array()) return &di["data"];
	return nullptr;
}

static void PreflightValidateDemoData(const std::vector<nlohmann::json>& expanded,
                                      const nlohmann::json& payload,
                                      bool includeData,
                                      ApplyResult& result)
{
	if (!includeData) return;
	if (!payload.contains("demoData") || !payload["demoData"].is_array()) return;
	const std::set<std::string> expectedTopLevel =
	    ExpectedTopLevelObjects(expanded);
	for (const auto& di : payload["demoData"]) {
		if (!di.is_object()) continue;
		const std::string fullName = DemoDataRef(di);
		const std::string kind = DemoDataKind(di, fullName);
		const std::string resolvedFullName =
		    ResolveDemoDataTarget(fullName, expectedTopLevel);
		const std::string lowerKind = LowerAscii(kind);
		if (lowerKind != "catalog" && lowerKind != "document" &&
		    lowerKind != "informationregister" &&
		    lowerKind != "accumulationregister") {
			AddDiagnostic(result, "preflight-data", kind, fullName,
			              _("demoData contains unsupported target kind"));
			continue;
		}
		if (expectedTopLevel.find(resolvedFullName) == expectedTopLevel.end() &&
		    !HostObjectExists(resolvedFullName)) {
			AddDiagnostic(result, "preflight-data", kind, fullName,
			              _("demoData target metadata object is not planned and does not exist"));
		}
	}
}

static bool CompileModuleSourcePreflight(const std::string& source)
{
	const wxString src = wxString::FromUTF8(source.c_str());
	const short priorStyle = ibCompileCode::GetCodeStyle();
	ibCompileCode probe;
	bool compileOk = probe.Compile(src);
	if (!compileOk) {
		ibCompileCode::SetCodeStyle(priorStyle == CODE_CES ? CODE_VES : CODE_CES);
		ibCompileCode alternateProbe;
		compileOk = alternateProbe.Compile(src);
	}
	ibCompileCode::SetCodeStyle(priorStyle);
	return compileOk;
}

static void CollectModuleSources(const nlohmann::json& value,
                                 std::vector<std::string>& out)
{
	if (value.is_object()) {
		for (auto& kv : value.items()) {
			if (kv.key() == "moduleCode" && kv.value().is_string()) {
				out.push_back(kv.value().get<std::string>());
			} else {
				CollectModuleSources(kv.value(), out);
			}
		}
	} else if (value.is_array()) {
		for (const auto& item : value) CollectModuleSources(item, out);
	}
}

static void PreflightCompileModules(const std::vector<nlohmann::json>& expanded,
                                    ApplyResult& result)
{
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		const std::string fullName = MutationFullName(m);
		const std::string kind = MutationKind(m, fullName);
		const nlohmann::json* propsObj = nullptr;
		if (m.contains("properties")) propsObj = &m["properties"];
		else if (m.contains("props")) propsObj = &m["props"];
		else if (m.contains("definition")) propsObj = &m["definition"];
		if (propsObj == nullptr) continue;
		std::vector<std::string> sources;
		CollectModuleSources(*propsObj, sources);
		for (const auto& source : sources) {
			if (source.empty()) continue;
			if (!CompileModuleSourcePreflight(source)) {
				AddDiagnostic(result, "preflight-code", kind, fullName,
				              _("moduleCode compile_check failed before apply"));
			}
		}
	}
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
		name = LeafNameFromPossiblyQualifiedName(item.get<std::string>());
	} else {
		name = LeafNameFromPossiblyQualifiedName(
		    ibTemplateWizardPayload::StringField(item, "name", "fullName", "path"));
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
	if (std::string(kind) == "Form") {
		std::string formType = ibTemplateWizardPayload::StringField(
		    props, "formType", "type", "kind");
		if (IsDefaultFormTypeTokenLocal(formType)) {
			props["formType"] = InferFormTypeToken(name, parentFullName);
		}
	}
	nlohmann::json child;
	child["op"] = "create";
	child["kind"] = kind;
	const std::string fullName = parentFullName + "." + container + "." + name;
	child["fullName"] = NormalizeTechnicalFullName(fullName);
	if (child["fullName"].get<std::string>() != fullName &&
	    !props.contains("synonym")) {
		props["synonym"] = name;
	}
	child["properties"] = props;
	out.push_back(std::move(child));
}

static void PushModuleEdit(std::vector<nlohmann::json>& out,
                           const std::string& parentFullName,
                           const char* moduleName,
                           const nlohmann::json& moduleValue)
{
	if (!moduleValue.is_string() && !moduleValue.is_object()) return;
	nlohmann::json patch = nlohmann::json::object();
	if (moduleValue.is_string()) {
		patch["moduleCode"] = moduleValue.get<std::string>();
	} else {
		patch = moduleValue;
		if (!patch.contains("moduleCode") && patch.contains("code")) {
			patch["moduleCode"] = patch["code"];
		}
	}
	if (!patch.contains("moduleCode") || !patch["moduleCode"].is_string()) {
		return;
	}
	nlohmann::json edit;
	edit["op"] = "edit";
	edit["kind"] = moduleName;
	edit["fullName"] = parentFullName + "." + moduleName;
	edit["properties"] = patch;
	out.push_back(std::move(edit));
}

static nlohmann::json MakeMutation(const char* op,
                                   const char* kind,
                                   const std::string& fullName,
                                   nlohmann::json props = nlohmann::json::object())
{
	nlohmann::json m;
	m["op"] = op;
	m["kind"] = kind;
	m["fullName"] = NormalizeTechnicalFullName(fullName);
	m["properties"] = std::move(props);
	return m;
}

static bool PlannedMutationExists(const std::vector<nlohmann::json>& expanded,
                                  const std::string& normalizedFullName)
{
	const std::string wanted = NormalizeTechnicalFullName(normalizedFullName);
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		if (NormalizeTechnicalFullName(MutationFullName(m)) == wanted) {
			return true;
		}
	}
	return false;
}

static std::string FindPlannedTopLevelByLooseKeys(
    const std::vector<nlohmann::json>& expanded,
    const std::string& kind,
    const std::vector<std::string>& keys)
{
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		if (NormalizedOp(m) != "create") continue;
		const std::string fullName = NormalizeTechnicalFullName(MutationFullName(m));
		const auto parts = SplitDottedPath(fullName);
		if (parts.size() != 2) continue;
		if (LowerAscii(MutationKind(m, fullName)) != LowerAscii(kind)) continue;
		const std::string objectKey = LooseAsciiKey(parts[1]);
		for (const auto& key : keys) {
			if (!key.empty() && objectKey.find(key) != std::string::npos) {
				return fullName;
			}
		}
	}
	return std::string();
}

static bool DocumentHasAttributeLike(const std::vector<nlohmann::json>& expanded,
                                     const std::string& documentFullName,
                                     const std::vector<std::string>& keys)
{
	const std::string docPrefix =
	    LowerAscii(NormalizeTechnicalFullName(documentFullName) + ".Attributes.");
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		const std::string fullName = NormalizeTechnicalFullName(MutationFullName(m));
		if (LowerAscii(fullName).find(docPrefix) != 0) continue;
		const auto parts = SplitDottedPath(fullName);
		if (parts.size() != 4) continue;
		const std::string attrKey = LooseAsciiKey(parts[3]);
		for (const auto& key : keys) {
			if (!key.empty() && attrKey.find(key) != std::string::npos) {
				return true;
			}
		}
	}
	return false;
}

static std::string DefaultDocumentModuleCode()
{
	return
	    "Procedure BeforeWrite(Cancel, WriteMode, PostingMode)\n"
	    "EndProcedure\n\n"
	    "Procedure Posting(Cancel, PostingMode)\n"
	    "EndProcedure\n\n"
	    "Procedure UndoPosting(Cancel)\n"
	    "EndProcedure\n\n"
	    "Procedure OnWrite(Cancel)\n"
	    "EndProcedure\n";
}

static std::string DefaultBusinessCommonModuleCode()
{
	return
	    "Function EnsureRequiredValue(Value, FieldName) Export\n"
	    "    If Value = Undefined Then\n"
	    "        Return False;\n"
	    "    EndIf;\n"
	    "    Return True;\n"
	    "EndFunction\n\n"
	    "Function CalculateDocumentTotal(TablePart, AmountField) Export\n"
	    "    TotalAmount = 0;\n"
	    "    Return TotalAmount;\n"
	    "EndFunction\n";
}

static void AddTopLevelIfMissing(std::vector<nlohmann::json>& expanded,
                                 const char* kind,
                                 const std::string& fullName,
                                 const std::string& synonym)
{
	const std::string normalized = NormalizeTechnicalFullName(fullName);
	if (PlannedMutationExists(expanded, normalized) || HostObjectExists(normalized)) {
		return;
	}
	nlohmann::json props;
	props["synonym"] = synonym;
	expanded.push_back(MakeMutation("create", kind, normalized, props));
}

static void AddCommonModuleWithCodeIfMissing(std::vector<nlohmann::json>& expanded,
                                             const std::string& fullName,
                                             const std::string& synonym)
{
	AddTopLevelIfMissing(expanded, "CommonModule", fullName, synonym);
	const std::string modulePath = NormalizeTechnicalFullName(fullName + ".ObjectModule");
	if (PlannedMutationExists(expanded, modulePath)) return;
	nlohmann::json props;
	props["moduleCode"] = DefaultBusinessCommonModuleCode();
	expanded.push_back(MakeMutation("edit", "ObjectModule", modulePath, props));
}

static void AddCatalogWithStandardForms(std::vector<nlohmann::json>& expanded,
                                        const std::string& fullName,
                                        const std::string& synonym)
{
	const std::string normalized = NormalizeTechnicalFullName(fullName);
	if (!PlannedMutationExists(expanded, normalized) && !HostObjectExists(normalized)) {
		nlohmann::json props;
		props["synonym"] = synonym;
		expanded.push_back(MakeMutation("create", "Catalog", normalized, props));
	}
	for (const auto& form : {
	         std::pair<const char*, const char*>("FormaElementa", "FormObject"),
	         std::pair<const char*, const char*>("FormaSpisku", "FormList"),
	         std::pair<const char*, const char*>("FormaVyboru", "FormSelect"),
	     }) {
		const std::string formPath =
		    normalized + ".Forms." + std::string(form.first);
		if (PlannedMutationExists(expanded, formPath) || HostObjectExists(formPath)) {
			continue;
		}
		nlohmann::json props;
		props["formType"] = form.second;
		expanded.push_back(MakeMutation("create", "Form", formPath, props));
	}
}

static void AddDocumentAttribute(std::vector<nlohmann::json>& expanded,
                                 const std::string& documentFullName,
                                 const std::string& name,
                                 const std::string& type,
                                 const std::string& synonym,
                                 int length = 0)
{
	const std::string fullName = NormalizeTechnicalFullName(
	    documentFullName + ".Attributes." + name);
	if (PlannedMutationExists(expanded, fullName) || HostObjectExists(fullName)) {
		return;
	}
	nlohmann::json props;
	props["type"] = type;
	props["synonym"] = synonym;
	props["required"] = false;
	if (length > 0) props["length"] = length;
	expanded.push_back(MakeMutation("create", "Attribute", fullName, props));
}

static void AddDocumentObjectModuleIfMissing(std::vector<nlohmann::json>& expanded,
                                             const std::string& documentFullName)
{
	const std::string modulePath =
	    NormalizeTechnicalFullName(documentFullName + ".ObjectModule");
	if (PlannedMutationExists(expanded, modulePath)) return;
	nlohmann::json props;
	props["moduleCode"] = DefaultDocumentModuleCode();
	expanded.push_back(MakeMutation("edit", "ObjectModule", modulePath, props));
}

static void EnrichBusinessDocumentBaseline(std::vector<nlohmann::json>& expanded)
{
	std::vector<std::string> documents;
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		if (NormalizedOp(m) != "create") continue;
		const std::string fullName = NormalizeTechnicalFullName(MutationFullName(m));
		const auto parts = SplitDottedPath(fullName);
		if (parts.size() == 2 &&
		    LowerAscii(MutationKind(m, fullName)) == "document") {
			documents.push_back(fullName);
		}
	}
	if (documents.empty()) return;

	AddCommonModuleWithCodeIfMissing(expanded, "CommonModule.BusinessWorkflow",
	                                 "Бізнес процеси");
	AddTopLevelIfMissing(expanded, "Role", "Role.Administrator",
	                     "Адміністратор");
	AddTopLevelIfMissing(expanded, "Role", "Role.Operator",
	                     "Оператор");
	AddTopLevelIfMissing(expanded, "Interface", "Interface.BusinessProcesses",
	                     "Бізнес процеси");
	AddTopLevelIfMissing(expanded, "Report", "Report.BusinessProcessResults",
	                     "Результати бізнес процесів");

	std::string organizationCatalog = FindPlannedTopLevelByLooseKeys(
	    expanded, "Catalog",
	    { "organiz", "organisation", "organization", "firm", "company",
	      "pidpryyemstvo", "pidpriemstvo" });
	if (organizationCatalog.empty()) {
		organizationCatalog = "Catalog.Organizatsiyi";
		AddCatalogWithStandardForms(expanded, organizationCatalog,
		                            "Організації");
	}

	std::string responsibleCatalog = FindPlannedTopLevelByLooseKeys(
	    expanded, "Catalog",
	    { "vidpovidal", "otvetstven", "responsible", "employee",
	      "sotrudnik", "users", "korystuvach" });
	if (responsibleCatalog.empty()) {
		responsibleCatalog = "Catalog.VidpovidalniOsoby";
		AddCatalogWithStandardForms(expanded, responsibleCatalog,
		                            "Відповідальні особи");
	}

	const auto orgParts = SplitDottedPath(organizationCatalog);
	const auto respParts = SplitDottedPath(responsibleCatalog);
	const std::string orgType = orgParts.size() == 2
	    ? "CatalogRef." + orgParts[1]
	    : "CatalogRef.Organizatsiyi";
	const std::string respType = respParts.size() == 2
	    ? "CatalogRef." + respParts[1]
	    : "CatalogRef.VidpovidalniOsoby";

	for (const auto& doc : documents) {
		if (!DocumentHasAttributeLike(
		        expanded, doc,
		        { "organiz", "organisation", "organization", "firm",
		          "company", "pidpryyemstvo", "pidpriemstvo" })) {
			AddDocumentAttribute(expanded, doc, "Organizatsiya", orgType,
			                     "Організація");
		}
		if (!DocumentHasAttributeLike(
		        expanded, doc,
		        { "vidpovidal", "otvetstven", "responsible", "employee",
		          "sotrudnik", "korystuvach" })) {
			AddDocumentAttribute(expanded, doc, "Vidpovidalnyi", respType,
			                     "Відповідальний");
		}
		if (!DocumentHasAttributeLike(
		        expanded, doc,
		        { "komentar", "comment", "prymitka", "primechanie",
		          "description", "opysanie" })) {
			AddDocumentAttribute(expanded, doc, "Komentar", "String",
			                     "Коментар", 500);
		}
		AddDocumentObjectModuleIfMissing(expanded, doc);
	}
}

static void ExpandTemplateMutation(const nlohmann::json& m,
                                   std::vector<nlohmann::json>& out)
{
	if (!m.is_object()) return;
	nlohmann::json normalized = m;
	const std::string op = NormalizedOp(m);
	std::string kind =
	    ibTemplateWizardPayload::StringField(m, "kind", "type");
	kind = NormalizedKind(kind);
	const nlohmann::json* propsForOwner = nullptr;
	if (m.contains("properties") && m["properties"].is_object()) {
		propsForOwner = &m["properties"];
	} else if (m.contains("props") && m["props"].is_object()) {
		propsForOwner = &m["props"];
	} else if (m.contains("definition") && m["definition"].is_object()) {
		propsForOwner = &m["definition"];
	}
	if (op == "create" && propsForOwner != nullptr) {
		const std::string owner = OwnerFullNameFromProperties(*propsForOwner);
		const std::string name = LeafNameFromPossiblyQualifiedName(
		    ibTemplateWizardPayload::StringField(m, "fullName", "name", "path"));
		if (!owner.empty() && !name.empty()) {
			if (kind == "Form") {
				normalized["kind"] = "Form";
				normalized["fullName"] = owner + ".Forms." +
				                         NormalizeTechnicalNameSegment(name);
				std::string formType = ibTemplateWizardPayload::StringField(
				    normalized["properties"], "formType", "type", "kind");
				if (IsDefaultFormTypeTokenLocal(formType)) {
					normalized["properties"]["formType"] =
					    InferFormTypeToken(name, owner);
				}
			} else if (kind == "Command") {
				normalized["kind"] = "Command";
				normalized["fullName"] = owner + ".Commands." +
				                         NormalizeTechnicalNameSegment(name);
			} else if (kind == "Module") {
				const std::string target = ModuleTargetFromOwner(owner,
				                                                 *propsForOwner);
				if (!target.empty()) {
					normalized["op"] = "edit";
					normalized["kind"] = LeafNameFromPossiblyQualifiedName(target);
					normalized["fullName"] = target;
				}
			}
		}
	}
	out.push_back(normalized);
	if (op != "create") return;
	std::string fullName =
	    ibTemplateWizardPayload::StringField(normalized, "fullName", "name", "path");
	if (fullName.empty()) return;
	const nlohmann::json* props = nullptr;
	if (normalized.contains("properties")) props = &normalized["properties"];
	else if (normalized.contains("props")) props = &normalized["props"];
	else if (normalized.contains("definition")) props = &normalized["definition"];
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
	for (const char* key : { "values", "enumValues", "enumerationValues", "items" }) {
		if (props->contains(key) && (*props)[key].is_array()) {
			for (const auto& item : (*props)[key]) {
				PushChildMutation(out, fullName, "Values", "EnumValue", item);
			}
		}
	}
	if (props->contains("forms") && (*props)["forms"].is_array()) {
		for (const auto& item : (*props)["forms"]) {
			PushChildMutation(out, fullName, "Forms", "Form", item);
		}
	}
	if (props->contains("templates") && (*props)["templates"].is_array()) {
		for (const auto& item : (*props)["templates"]) {
			PushChildMutation(out, fullName, "Templates", "Template", item);
		}
	}
	if (props->contains("printForms") && (*props)["printForms"].is_array()) {
		for (const auto& item : (*props)["printForms"]) {
			PushChildMutation(out, fullName, "Templates", "Template", item);
		}
	}
	if (props->contains("tabularSections") &&
	    (*props)["tabularSections"].is_array()) {
		for (const auto& item : (*props)["tabularSections"]) {
			PushChildMutation(out, fullName, "TabularSections",
			                  "TabularSection", item);
		}
	}
	if (props->contains("objectModule")) {
		PushModuleEdit(out, fullName, "ObjectModule", (*props)["objectModule"]);
	}
	if (props->contains("module")) {
		PushModuleEdit(out, fullName, "ObjectModule", (*props)["module"]);
	}
	if (props->contains("managerModule")) {
		PushModuleEdit(out, fullName, "ManagerModule", (*props)["managerModule"]);
	}
	if (props->contains("modules") && (*props)["modules"].is_array()) {
		for (const auto& item : (*props)["modules"]) {
			if (!item.is_object()) continue;
			std::string moduleName =
			    ibTemplateWizardPayload::StringField(item, "kind", "name", "type");
			moduleName = NormalizedKind(moduleName);
			if (moduleName == "Module") moduleName = "ObjectModule";
			if (moduleName == "ObjectModule" || moduleName == "ManagerModule") {
				PushModuleEdit(out, fullName, moduleName.c_str(), item);
			}
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
		if (const nlohmann::json* rows = DemoDataRows(di)) {
			total += static_cast<int>(rows->size());
		}
	}
	return total;
}

static void WriteApplyDiagnostics(const ApplyResult& result)
{
	wxString dir;
	if (appData != nullptr) {
		dir = appData->GetFileDirectory();
	}
	if (dir.IsEmpty()) {
		dir = wxStandardPaths::Get().GetUserLocalDataDir();
		if (dir.IsEmpty()) {
			dir = wxFileName::GetTempDir();
		}
		wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	}
	nlohmann::json report;
	report["successCount"] = result.successCount;
	report["failureCount"] = result.failureCount;
	report["expectedObjectCount"] = result.expectedObjectCount;
	report["missingObjectCount"] = result.missingObjectCount;
	report["preflightFailureCount"] = result.preflightFailureCount;
	report["completenessWarningCount"] = result.completenessWarningCount;
	report["completenessScore"] = result.completenessScore;
	report["expectedDataRows"] = result.expectedDataRows;
	report["insertedDataRows"] = result.insertedDataRows;
	report["skippedDataRows"] = result.skippedDataRows;
	report["ops"] = nlohmann::json::array();
	for (const auto& op : result.ops) {
		nlohmann::json item;
		item["op"] = std::string(op.op.utf8_str());
		item["kind"] = std::string(op.kind.utf8_str());
		item["fullName"] = std::string(op.fullName.utf8_str());
		item["success"] = op.success;
		item["error"] = std::string(op.error.utf8_str());
		report["ops"].push_back(std::move(item));
	}
	auto writeReport = [&](const wxFileName& path) {
		wxFileName::Mkdir(path.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
		std::ofstream out(std::string(path.GetFullPath().utf8_str()));
		if (!out.good()) return false;
		out << report.dump(2);
		return true;
	};
	wxFileName diagPath(dir, wxT("template-wizard-last-apply.json"));
	bool wrote = writeReport(diagPath);
	wxFileName logPath(wxStandardPaths::Get().GetUserLocalDataDir(),
	                   wxT("template-wizard-last-apply.json"));
	if (!logPath.GetPath().IsEmpty()) {
		wrote = writeReport(logPath) || wrote;
	}
	wxFileName tmpPath(wxFileName::GetTempDir(),
	                   wxT("template-wizard-last-apply.json"));
	wrote = writeReport(tmpPath) || wrote;
	if (!wrote) {
		wxLogWarning("Template Wizard: failed to write apply diagnostics");
	}
}

static bool IsValidationTarget(const nlohmann::json& m,
                               const std::string& opStr,
                               const std::string& kindStr,
                               const std::string& fullName)
{
	if (opStr != "create" && opStr != "edit") return false;
	if (fullName.empty()) return false;
	if (IsConfigurationMutation(kindStr, fullName)) return false;
	const std::string lowerName = LowerAscii(fullName);
	if (lowerName.find(".objectmodule") != std::string::npos ||
	    lowerName.find(".managermodule") != std::string::npos) {
		return false; // HostMetaQuery does not expose singleton modules yet.
	}
	(void)m;
	return true;
}

static void ValidateExpectedMetadata(const std::vector<nlohmann::json>& expanded,
                                     ApplyResult& result)
{
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		const std::string opStr = NormalizedOp(m);
		std::string fullName = NormalizeTechnicalFullName(
		    ibTemplateWizardPayload::StringField(m, "fullName", "name", "path"));
		std::string kindStr = ibTemplateWizardPayload::StringField(m, "kind", "type");
		if (kindStr.empty()) {
			kindStr = ibTemplateWizardPayload::InferKindFromFullName(fullName);
		}
		kindStr = NormalizedKind(kindStr);
		if (!IsValidationTarget(m, opStr, kindStr, fullName)) continue;

		++result.expectedObjectCount;
		char* jsonOut = nullptr;
		char* err = nullptr;
		const int rc = metaBridge::HostMetaQuery(fullName.c_str(), nullptr,
		                                         &jsonOut, &err);
		if (jsonOut != nullptr) std::free(jsonOut);
		if (rc == 0) {
			if (err != nullptr) std::free(err);
			continue;
		}
		OpResult op;
		op.op = wxT("validate");
		op.kind = wxString::FromUTF8(kindStr.c_str());
		op.fullName = wxString::FromUTF8(fullName.c_str());
		op.success = false;
		op.error = err != nullptr
		    ? wxString::FromUTF8(err)
		    : _("expected metadata object not found after apply");
		if (err != nullptr) std::free(err);
		result.ops.push_back(op);
		++result.failureCount;
		++result.missingObjectCount;
		wxLogWarning("Template Wizard: validation failed: %s %s: %s",
		             op.kind, op.fullName,
		             op.error.IsEmpty() ? wxString(_("(нет диагностики)"))
		                                : op.error);
	}
}

static void MaterializeTemplateForms(const std::vector<nlohmann::json>& expanded,
                                     ApplyResult& result)
{
	for (const auto& m : expanded) {
		if (!m.is_object()) continue;
		const std::string opStr = NormalizedOp(m);
		if (opStr != "create" && opStr != "edit") continue;
		const std::string fullName = NormalizeTechnicalFullName(MutationFullName(m));
		const auto parts = SplitDottedPath(fullName);
		if (parts.size() != 4 || LowerAscii(parts[2]) != "forms") continue;
		const std::string kind = MutationKind(m, fullName);
		if (LowerAscii(kind) != "form" &&
		    LowerAscii(kind) != "itemform" &&
		    LowerAscii(kind) != "listform" &&
		    LowerAscii(kind) != "choiceform" &&
		    LowerAscii(kind) != "selectionform") {
			continue;
		}

		ibValueMetaObject* parent =
		    FindMetaObjectByFullName(parts[0] + "." + parts[1]);
		auto* generic = dynamic_cast<ibValueMetaObjectGenericData*>(parent);
		auto* form = dynamic_cast<ibValueMetaObjectFormBase*>(
		    FindMetaObjectByFullName(fullName));
		if (generic == nullptr || form == nullptr) continue;

		const nlohmann::json* propsObj = nullptr;
		if (m.contains("properties") && m["properties"].is_object()) {
			propsObj = &m["properties"];
		} else if (m.contains("props") && m["props"].is_object()) {
			propsObj = &m["props"];
		} else if (m.contains("definition") && m["definition"].is_object()) {
			propsObj = &m["definition"];
		}
		std::string formType = propsObj != nullptr
		    ? ibTemplateWizardPayload::StringField(*propsObj,
		                                           "formType", "type", "kind")
		    : std::string();
		if (IsDefaultFormTypeTokenLocal(formType)) {
			formType = InferFormTypeToken(parts[3], parts[0] + "." + parts[1]);
		}

		wxString error;
		if (!ForceFormType(form, formType, error)) {
			OpResult op;
			op.op = wxT("form-materialize");
			op.kind = wxT("Form");
			op.fullName = wxString::FromUTF8(fullName.c_str());
			op.success = false;
			op.error = error;
			result.ops.push_back(op);
			++result.failureCount;
			continue;
		}

		form->SetFormData(wxMemoryBuffer());
		if (!generic->MaterializeFormData(form, error)) {
			OpResult op;
			op.op = wxT("form-materialize");
			op.kind = wxT("Form");
			op.fullName = wxString::FromUTF8(fullName.c_str());
			op.success = false;
			op.error = error;
			result.ops.push_back(op);
			++result.failureCount;
			continue;
		}

		OpResult op;
		op.op = wxT("form-materialize");
		op.kind = wxT("Form");
		op.fullName = wxString::FromUTF8(fullName.c_str());
		op.success = true;
		result.ops.push_back(op);
		++result.successCount;
	}
}

static ibValueMetaObject* FindTopLevelObject(const std::string& fullName)
{
	if (activeMetaData == nullptr) return nullptr;
	ibValueMetaObject* root = activeMetaData->GetCommonMetaObject();
	if (root == nullptr) return nullptr;
	const size_t dot = fullName.find('.');
	if (dot == std::string::npos) return nullptr;
	const std::string kind = NormalizedKind(fullName.substr(0, dot));
	const std::string lowerKind = LowerAscii(kind);
	const ibClassID clsid =
	    lowerKind == "catalog" ? g_metaCatalogCLSID :
	    lowerKind == "document" ? g_metaDocumentCLSID :
	    lowerKind == "informationregister" ? g_metaInformationRegisterCLSID :
	    lowerKind == "accumulationregister" ? g_metaAccumulationRegisterCLSID :
	    0;
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

static bool ResolveCatalogReference(const ibValueMetaObjectAttributeBase* attr,
                                    const ibValue& rawValue,
                                    ibValue& outValue)
{
	if (attr == nullptr || activeMetaData == nullptr || rawValue.IsEmpty()) {
		return false;
	}
	if (rawValue.GetType() != ibValueTypes::TYPE_STRING) return false;
	for (const auto& clsid : attr->GetTypeDesc().GetClsidList()) {
		const ibCtorMetaValueType* ctor = activeMetaData->GetTypeCtor(clsid);
		if (ctor == nullptr ||
		    ctor->GetMetaTypeCtor() != ibCtorObjectMetaType_Reference) {
			continue;
		}
		const auto* target =
		    dynamic_cast<const ibValueMetaObjectCatalog*>(ctor->GetMetaObject());
		if (target == nullptr) continue;
		ibValueManagerDataObjectCatalog manager(target);
		ibValuePtr<ibValueReferenceDataObject> ref(
		    manager.FindByCode(rawValue));
		if (ref && !ref->IsEmptyRef()) {
			outValue = ref;
			return true;
		}
		ref = manager.FindByDescription(rawValue);
		if (ref && !ref->IsEmptyRef()) {
			outValue = ref;
			return true;
		}
	}
	return false;
}

static ibValue RowValueForAttribute(const ibValueMetaObjectAttributeBase* attr,
                                    const nlohmann::json& value,
                                    const wxString& key)
{
	ibValue rawValue = JsonToValue(value, key);
	ibValue refValue;
	if (ResolveCatalogReference(attr, rawValue, refValue)) return refValue;
	return rawValue;
}

static ibValueMetaObjectAttributeBase* FindRowAttribute(
    const ibValueMetaObjectCompositeData* meta,
    const wxString& key)
{
	if (meta == nullptr) return nullptr;
	if (auto* catalog =
	        dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(meta)) {
		if (key.CmpNoCase(wxT("Code")) == 0) return catalog->GetDataCode();
		if (key.CmpNoCase(wxT("Description")) == 0) {
			return catalog->GetDataDescription();
		}
		if (key.CmpNoCase(wxT("Parent")) == 0) return catalog->GetDataParent();
	}
	if (auto* document = dynamic_cast<const ibValueMetaObjectDocument*>(meta)) {
		if (key.CmpNoCase(wxT("Number")) == 0) return document->GetDocumentNumber();
		if (key.CmpNoCase(wxT("Date")) == 0) return document->GetDocumentDate();
	}
	for (auto* candidate : meta->GetGenericAttributeArrayObject()) {
		if (candidate == nullptr || candidate->IsDeleted()) continue;
		wxString objectName;
		if (!candidate->GetObjectNameAsString(objectName)) continue;
		if (objectName.CmpNoCase(key) == 0) return candidate;
	}
	return nullptr;
}

static bool ApplyRowFields(ibValueDataObject* object,
                           const ibValueMetaObjectCompositeData* meta,
                           const nlohmann::json& row,
                           wxString& error)
{
	if (object == nullptr || !row.is_object()) return false;
	if (meta == nullptr) return false;
	for (auto& [rawKey, value] : row.items()) {
		if (value.is_array() || value.is_object()) continue;
		const wxString key = CanonicalRowKey(rawKey);
		ibValueMetaObjectAttributeBase* attr = FindRowAttribute(meta, key);
		if (attr == nullptr) continue;
		if (!object->SetValueByMetaID(attr->GetMetaID(),
		                              RowValueForAttribute(attr, value, key))) {
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

static bool ApplyRecordRowFields(ibValueModelTableBase::ibValueModelReturnLine* line,
                                 const ibValueMetaObjectCompositeData* meta,
                                 const nlohmann::json& row,
                                 wxString& error)
{
	if (line == nullptr || !row.is_object()) return false;
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
		if (!line->SetValueByMetaID(attr->GetMetaID(),
		                            RowValueForAttribute(attr, value, key))) {
			if (attr->FillCheck()) {
				error = wxString::Format(_("failed to set field '%s'"), key);
				return false;
			}
			wxLogWarning("Template Wizard: skipped demo-data register field '%s' "
			             "because the raw JSON value does not match the "
			             "metadata attribute type yet.",
			             key);
		}
	}
	return true;
}

static bool ApplyTabularRows(ibValueRecordDataObjectRef* object,
                             const ibValueMetaObjectRecordData* meta,
                             const nlohmann::json& row,
                             wxString& error)
{
	if (object == nullptr || meta == nullptr || !row.is_object()) return false;
	for (auto& [rawKey, value] : row.items()) {
		if (!value.is_array()) continue;
		const wxString tableName = wxString::FromUTF8(rawKey.c_str());
		ibValueMetaObjectTableData* tableMeta = nullptr;
		for (auto* candidate : meta->GetTableArrayObject()) {
			if (candidate == nullptr || candidate->IsDeleted()) continue;
			wxString objectName;
			if (!candidate->GetObjectNameAsString(objectName)) continue;
			if (objectName.CmpNoCase(tableName) == 0) {
				tableMeta = candidate;
				break;
			}
		}
		if (tableMeta == nullptr) continue;
		ibValueModel* model = nullptr;
		if (!object->GetModel(model, tableMeta->GetMetaID()) || model == nullptr) {
			error = wxString::Format(_("failed to open tabular section '%s'"),
			                         tableName);
			return false;
		}
		auto* table = dynamic_cast<ibValueTabularSectionDataObjectBase*>(model);
		if (table == nullptr) {
			error = wxString::Format(_("target '%s' is not a tabular section"),
			                         tableName);
			return false;
		}
		for (const auto& item : value) {
			if (!item.is_object()) continue;
			const long index = table->AppendRow();
			ibValuePtr<ibValueModelTableBase::ibValueModelReturnLine> line(
			    table->GetRowAt(table->GetItem(index)));
			if (!line) {
				error = wxString::Format(_("failed to add row to '%s'"),
				                         tableName);
				return false;
			}
			if (!ApplyRecordRowFields(line, tableMeta, item, error)) {
				return false;
			}
		}
	}
	return true;
}

static bool InsertRegisterRow(ibValueMetaObjectRegisterData* meta,
                              const nlohmann::json& row,
                              wxString& error)
{
	if (meta == nullptr) return false;
	ibValuePtr<ibValueRecordSetObject> recordSet(
	    meta->CreateRecordSetObjectValue());
	if (!recordSet) {
		error = _("failed to create register record set");
		return false;
	}
	const long index = recordSet->AppendRow();
	ibValuePtr<ibValueModelTableBase::ibValueModelReturnLine> line(
	    recordSet->GetRowAt(recordSet->GetItem(index)));
	if (!line) {
		error = _("failed to create register row");
		return false;
	}
	if (!ApplyRecordRowFields(line, meta, row, error)) return false;
	return recordSet->WriteRecordSet(/*replace=*/false, /*clearTable=*/true);
}

static bool InsertDataRow(const std::string& kind,
                          const std::string& fullName,
                          const nlohmann::json& row,
                          bool postAfterInsert,
                          wxString& error)
{
	(void)postAfterInsert; // posting needs reference resolution; insert visible rows first.
	const std::string normalizedFullName = NormalizeTechnicalFullName(fullName);
	ibValueMetaObject* meta = FindTopLevelObject(normalizedFullName);
	if (meta == nullptr && normalizedFullName != fullName) {
		meta = FindTopLevelObject(fullName);
	}
	if (meta == nullptr) {
		error = _("metadata object not found");
		return false;
	}
	const std::string lowerKind = LowerAscii(NormalizedKind(kind.empty()
	    ? ibTemplateWizardPayload::InferKindFromFullName(fullName)
	    : kind));
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
		if (!ApplyRowFields(object, catalog, row, error)) return false;
		if (!ApplyTabularRows(object, catalog, row, error)) return false;
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
		if (!ApplyRowFields(object, document, row, error)) return false;
		if (!ApplyTabularRows(object, document, row, error)) return false;
		if (postAfterInsert) {
			try {
				if (object->WriteObject(
				        ibDocumentWriteMode::ibDocumentWriteMode_Posting,
				        ibDocumentPostingMode::ibDocumentPostingMode_Regular)) {
					return true;
				}
			}
			catch (const ibBackendException& err) {
				wxLogWarning("Template Wizard: document posting failed for %s: %s",
				             fullName, err.GetErrorDescription());
			}
			catch (const std::exception& err) {
				wxLogWarning("Template Wizard: document posting failed for %s: %s",
				             fullName, wxString::FromUTF8(err.what()));
			}
		}
		return object->WriteObject(
		    ibDocumentWriteMode::ibDocumentWriteMode_Write,
		    ibDocumentPostingMode::ibDocumentPostingMode_Regular);
	}
	if (lowerKind == "informationregister" ||
	    lowerKind == "accumulationregister") {
		auto* reg = dynamic_cast<ibValueMetaObjectRegisterData*>(meta);
		if (reg == nullptr) {
			error = _("target is not a register");
			return false;
		}
		return InsertRegisterRow(reg, row, error);
	}
	error = _("data rows for this metadata kind are not supported yet");
	return false;
}

ApplyResult Apply(const wxString& responseJson, bool includeData,
                  ProgressCallback progress)
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
		WriteApplyDiagnostics(result);
		return result;
	}
	ReplaceWithJsonTextEnvelope(parsed);
	const nlohmann::json* payload = UnwrapPayload(parsed);
	const nlohmann::json* mutations =
	    ibTemplateWizardPayload::PickMutations(*payload);
	if (mutations == nullptr || mutations->empty()) {
		OpResult op;
		op.op    = wxT("(empty)");
		op.error = wxT("no template mutations in response");
		result.ops.push_back(op);
		result.failureCount = 1;
		WriteApplyDiagnostics(result);
		return result;
	}

	std::vector<nlohmann::json> expanded;
	for (const auto& m : *mutations) {
		ExpandTemplateMutation(m, expanded);
	}
	EnrichBusinessDocumentBaseline(expanded);
	SortMutationsByDependency(expanded);
	PreflightValidateMutations(expanded, result);
	EvaluateCompleteness(expanded, *payload, includeData, result);
	PreflightValidateDemoData(expanded, *payload, includeData, result);
	PreflightCompileModules(expanded, result);
	if (result.preflightFailureCount > 0) {
		WriteApplyDiagnostics(result);
		return result;
	}

	// Walk mutations sequentially. Per spec we prefer partial-apply over
	// all-or-nothing: each metaBridge mutation is individually undoable
	// via Ctrl+Z, so a failure mid-walk leaves the user with a recoverable
	// state.
	const int totalOps = static_cast<int>(expanded.size());
	int currentOp = 0;
	for (const auto& m : expanded) {
		OpResult op;
		++currentOp;
		if (!m.is_object()) {
			op.op    = wxT("(skip)");
			op.error = wxT("non-object entry in mutations[]");
			if (progress) progress(currentOp, totalOps, op);
			result.ops.push_back(op);
			++result.failureCount;
			continue;
		}
		const std::string opStr = NormalizedOp(m);
		const std::string originalFullName =
		    ibTemplateWizardPayload::StringField(m, "fullName", "name", "path");
		std::string fullName = NormalizeTechnicalFullName(originalFullName);
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
		nlohmann::json propsJson = propsObj != nullptr
		    ? *propsObj
		    : nlohmann::json::object();
		if (fullName != originalFullName && !propsJson.contains("synonym")) {
			propsJson["synonym"] = LeafNameFromPossiblyQualifiedName(originalFullName);
		}
		NormalizePropertyTypes(propsJson);
		const std::string props = propsJson.dump();

		op.op       = wxString::FromUTF8(opStr.c_str());
		op.kind     = wxString::FromUTF8(kindStr.c_str());
		op.fullName = wxString::FromUTF8(fullName.c_str());
		if (progress) progress(currentOp, totalOps, op);

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
			if (opStr == "create" && IsAlreadyExistsError(op.error)) {
				op.success = true;
				op.error.clear();
				++result.successCount;
			} else {
				++result.failureCount;
			}
		}
		if (err != nullptr) std::free(err);
		result.ops.push_back(op);
		if (!op.success) {
			wxLogWarning("Template Wizard: metadata op failed: %s %s %s: %s",
			             op.op, op.kind, op.fullName,
			             op.error.IsEmpty() ? wxString(_("(нет диагностики)"))
			                                : op.error);
		}
	}

	MaterializeTemplateForms(expanded, result);
	ValidateExpectedMetadata(expanded, result);

	if (result.failureCount == 0 && activeMetaData != nullptr) {
		if (!activeMetaData->SaveDatabase()) {
			OpResult op;
			op.op = wxT("save-metadata");
			op.error = _("failed to save metadata after template apply");
			const wxString detail = RestructureDiagnostics();
			if (!detail.IsEmpty()) op.error += wxT(": ") + detail;
			result.ops.push_back(op);
			++result.failureCount;
		}
	}

	if (includeData) {
		const int totalRows = CountDemoRows(*payload);
		result.expectedDataRows = totalRows;
		wxLogMessage("Template Wizard: demo-data phase starting, rows=%d, "
		             "metadataFailures=%d",
		             totalRows, result.failureCount);
		if (totalRows > 0) {
			wxString prepareError;
			if (!PrepareConfigurationForDemoData(expanded, prepareError)) {
				OpResult op;
				op.op = wxT("data-prepare");
				op.error = prepareError.IsEmpty()
				    ? _("failed to prepare configuration before inserting demo data")
				    : prepareError;
				result.ops.push_back(op);
				++result.failureCount;
				result.skippedDataRows += totalRows;
			} else {
				ibApplicationData::ScopedDesignerDataWrite writeScope;
				const std::set<std::string> expectedTopLevel =
				    ExpectedTopLevelObjects(expanded);
				for (const auto& di : (*payload)["demoData"]) {
					if (!di.is_object()) continue;
					const std::string fullName = DemoDataRef(di);
					const std::string kind = DemoDataKind(di, fullName);
					const std::string normalizedFullName =
					    ResolveDemoDataTarget(fullName, expectedTopLevel);
					const bool postAfterInsert =
					    di.value("postAfterInsert", false);
					const nlohmann::json* rows = DemoDataRows(di);
					if (rows == nullptr) continue;
					for (const auto& row : *rows) {
						OpResult op;
						op.op = wxT("data-insert");
						op.kind = wxString::FromUTF8(kind.c_str());
						op.fullName = wxString::FromUTF8(normalizedFullName.c_str());
						wxString err;
						if (InsertDataRow(kind, normalizedFullName, row,
						                  postAfterInsert, err)) {
							op.success = true;
							++result.successCount;
							++result.insertedDataRows;
						} else {
							op.success = false;
							op.error = err;
							++result.failureCount;
							++result.skippedDataRows;
							wxLogWarning("Template Wizard: demo-data insert failed: %s %s: %s",
							             op.kind, op.fullName,
							             op.error.IsEmpty()
							                 ? wxString(_("(нет диагностики)"))
							                 : op.error);
						}
						result.ops.push_back(op);
					}
				}
			}
		} else {
			OpResult op;
			op.op = wxT("data-skip");
			op.error = _("template response contains no demo-data rows");
			op.success = true;
			result.ops.push_back(op);
		}
	}

	WriteApplyDiagnostics(result);
	return result;
}

}  // namespace ibTemplateWizardApplier
