/////////////////////////////////////////////////////////////////////////////
// basXmlReader — implementation.
//
// Strategy:
//  1. ImportXmlConfiguration() opens the root Configuration.xml,
//     enumerates ChildObjects/<Kind>Name entries, and for each
//     supported kind opens "<objectsRoot>/<KindDir>/<Name>.xml".
//  2. ImportSingleObjectFile() dispatches on the root element kind,
//     emits a `meta_create` mutation for the object, then walks
//     ChildObjects to emit Attribute / TabularSection / EnumValue
//     children.
//  3. Forms / Templates / Commands are listed but treated as
//     DEFERRED — only stub mutations are emitted (form blob
//     reconstruction is the t1-002 architectural blocker noted in
//     the spec). Modules — likewise only stubbed at v1 (extracting
//     module text from BAS .cf or per-object subdirs is out of
//     scope for this commit).
//
// We deliberately keep XML traversal pattern simple — wxXmlDocument
// loads the whole tree in memory, ~50KB per object file. Memory
// budget is fine for the BAS corpus (~3200 objects = a few hundred
// MB peak; the import is a one-shot batch operation, not a hot path).
/////////////////////////////////////////////////////////////////////////////

#include "basXmlReader.hpp"
#include "basMapping.hpp"

#include <wx/xml/xml.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/wfstream.h>
#include <wx/sstream.h>
#include <wx/log.h>

#include <algorithm>
#include <map>

namespace migration {
namespace bas {
namespace {

// ----- XML helpers --------------------------------------------------------

// Find the first child of `parent` whose tag name equals `tag` — ignoring
// namespace prefixes that BAS sprinkles everywhere (v8:, xr:, cfg:, ...).
// Returns nullptr when no match.
wxXmlNode* FindChild(wxXmlNode* parent, const wxString& tag)
{
	if (parent == nullptr) return nullptr;
	for (wxXmlNode* n = parent->GetChildren(); n != nullptr; n = n->GetNext()) {
		if (n->GetType() != wxXML_ELEMENT_NODE) continue;
		const wxString name = n->GetName();
		// Match either the bare local name or anything after a `:` prefix.
		if (name == tag) return n;
		const int colon = name.Find(wxT(':'));
		if (colon != wxNOT_FOUND && name.Mid(colon + 1) == tag) return n;
	}
	return nullptr;
}

// Iterate over all element children whose local name matches `tag`. Pass
// nullptr for `tag` to iterate every child.
struct ChildIter {
	wxXmlNode* cur;
	const wxString* needle;  // nullptr -> any
	bool MatchesTag(wxXmlNode* n) const
	{
		if (needle == nullptr) return true;
		const wxString name = n->GetName();
		if (name == *needle) return true;
		const int colon = name.Find(wxT(':'));
		return colon != wxNOT_FOUND && name.Mid(colon + 1) == *needle;
	}
	void AdvanceToMatch()
	{
		while (cur != nullptr) {
			if (cur->GetType() == wxXML_ELEMENT_NODE && MatchesTag(cur)) return;
			cur = cur->GetNext();
		}
	}
};

ChildIter BeginChildren(wxXmlNode* parent, const wxString* tag = nullptr)
{
	ChildIter it{ parent ? parent->GetChildren() : nullptr, tag };
	it.AdvanceToMatch();
	return it;
}

void StepChildren(ChildIter& it)
{
	if (it.cur != nullptr) it.cur = it.cur->GetNext();
	it.AdvanceToMatch();
}

// LocalName(node) -> the tag without any "ns:" prefix.
wxString LocalName(const wxXmlNode* n)
{
	if (n == nullptr) return wxString();
	const wxString name = n->GetName();
	const int colon = name.Find(wxT(':'));
	return colon == wxNOT_FOUND ? name : name.Mid(colon + 1);
}

// Pull the text content of a child element by tag; returns empty wxString
// when missing.
wxString GetChildText(wxXmlNode* parent, const wxString& tag)
{
	wxXmlNode* c = FindChild(parent, tag);
	return c ? c->GetNodeContent() : wxString();
}

// Read <Synonym>...</Synonym> -> { "ru": "...", "uk": "..." } JSON map.
// BAS uses <v8:item><v8:lang>ru</v8:lang><v8:content>...</v8:content>
// </v8:item> children.
nlohmann::json ReadSynonymBlock(wxXmlNode* synonymNode)
{
	nlohmann::json out = nlohmann::json::object();
	if (synonymNode == nullptr) return out;
	const wxString itemTag(wxT("item"));
	for (auto it = BeginChildren(synonymNode, &itemTag); it.cur != nullptr; StepChildren(it)) {
		const wxString lang    = GetChildText(it.cur, wxT("lang"));
		const wxString content = GetChildText(it.cur, wxT("content"));
		if (!lang.empty()) {
			out[std::string(lang.utf8_str().data())] =
				std::string(content.utf8_str().data());
		}
	}
	return out;
}

// ----- glob filter --------------------------------------------------------

bool MatchesGlob(const wxString& name, const wxString& pattern)
{
	if (pattern == wxT("*") || pattern == name) return true;
	if (pattern.EndsWith(wxT(".*"))) {
		const wxString prefix = pattern.Left(pattern.length() - 2);
		// Match if name starts with "prefix." (so "Catalog.*" matches
		// "Catalog.Контрагенты" but not "CatalogFoo").
		return name == prefix
		    || name.StartsWith(prefix + wxT("."));
	}
	return false;
}

bool PassesFilter(const wxString& fullName, const ObjectFilter& filter)
{
	if (filter.empty()) return true;
	for (const auto& p : filter) {
		if (MatchesGlob(fullName, p)) return true;
	}
	return false;
}

// ----- per-kind directory mapping -----------------------------------------

// Map an OES kind back to the BAS subdirectory name. BAS uses plural-
// pascal-case subdirectories. The reverse mapping is needed because the
// Configuration.xml ChildObjects list uses singular kind tags.
wxString DirectoryForBasKind(const wxString& basKind)
{
	struct Row { const wxChar* kind; const wxChar* dir; };
	static const Row table[] = {
		{ wxT("Catalog"),                    wxT("Catalogs") },
		{ wxT("Document"),                   wxT("Documents") },
		{ wxT("Enum"),                       wxT("Enums") },
		{ wxT("Constant"),                   wxT("Constants") },
		{ wxT("InformationRegister"),        wxT("InformationRegisters") },
		{ wxT("AccumulationRegister"),       wxT("AccumulationRegisters") },
		{ wxT("AccountingRegister"),         wxT("AccountingRegisters") },
		{ wxT("ChartOfAccounts"),            wxT("ChartsOfAccounts") },
		{ wxT("ChartOfCharacteristicTypes"), wxT("ChartsOfCharacteristicTypes") },
		{ wxT("ChartOfCalculationTypes"),    wxT("ChartsOfCalculationTypes") },
		{ wxT("CalculationRegister"),        wxT("CalculationRegisters") },
		{ wxT("Report"),                     wxT("Reports") },
		{ wxT("DataProcessor"),              wxT("DataProcessors") },
		{ wxT("CommonModule"),               wxT("CommonModules") },
		{ wxT("CommonForm"),                 wxT("CommonForms") },
		{ wxT("CommonCommand"),              wxT("CommonCommands") },
		{ wxT("CommonTemplate"),             wxT("CommonTemplates") },
		{ wxT("CommonPicture"),              wxT("CommonPictures") },
		{ wxT("CommonAttribute"),            wxT("CommonAttributes") },
		{ wxT("DefinedType"),                wxT("DefinedTypes") },
		{ wxT("Subsystem"),                  wxT("Subsystems") },
		{ wxT("Role"),                       wxT("Roles") },
		{ wxT("ExchangePlan"),               wxT("ExchangePlans") },
		{ wxT("DocumentJournal"),            wxT("DocumentJournals") },
		{ wxT("DocumentNumerator"),          wxT("DocumentNumerators") },
		{ wxT("FunctionalOption"),           wxT("FunctionalOptions") },
		{ wxT("FunctionalOptionsParameter"), wxT("FunctionalOptionsParameters") },
		{ wxT("EventSubscription"),          wxT("EventSubscriptions") },
		{ wxT("ScheduledJob"),               wxT("ScheduledJobs") },
		{ wxT("SettingsStorage"),            wxT("SettingsStorages") },
		{ wxT("StyleItem"),                  wxT("StyleItems") },
		{ wxT("WebService"),                 wxT("WebServices") },
		{ wxT("HTTPService"),                wxT("HTTPServices") },
		{ wxT("WSReference"),                wxT("WSReferences") },
		{ wxT("XDTOPackage"),                wxT("XDTOPackages") },
		{ wxT("BusinessProcess"),            wxT("BusinessProcesses") },
		{ wxT("Task"),                       wxT("Tasks") },
		{ wxT("FilterCriterion"),            wxT("FilterCriteria") },
		{ wxT("CommandGroup"),               wxT("CommandGroups") },
		{ wxT("Language"),                   wxT("Languages") },
		{ wxT("SessionParameter"),           wxT("SessionParameters") },
	};
	constexpr std::size_t n = sizeof(table) / sizeof(table[0]);
	for (std::size_t i = 0; i < n; ++i) {
		if (basKind == table[i].kind) return wxString(table[i].dir);
	}
	return wxString();
}

// ----- attribute / dimension / resource extraction ------------------------

// Read a single <Type>...</Type> element, picking the first non-deleted
// child type entry. BAS allows compound types via multiple <v8:Type>
// children; we emit a warning + use the first one. Sets `refTarget` when
// the chosen type is a reference.
//
// `compoundFlag` is set when more than one type entry exists, so the caller
// can record a warning.
wxString ReadAttributeType(wxXmlNode* attrNode, wxString& refTarget,
                            bool& compoundFlag, wxString& length,
                            wxString& precision, wxString& scale)
{
	refTarget.Clear();
	compoundFlag = false;
	length.Clear(); precision.Clear(); scale.Clear();

	wxXmlNode* typeNode = FindChild(attrNode, wxT("Type"));
	if (typeNode == nullptr) return wxString();

	const wxString typeTag(wxT("Type"));
	const wxString strQualTag(wxT("StringQualifiers"));
	const wxString numQualTag(wxT("NumberQualifiers"));

	int typeCount = 0;
	wxString first;
	for (auto it = BeginChildren(typeNode, &typeTag); it.cur != nullptr;
	     StepChildren(it))
	{
		++typeCount;
		if (typeCount == 1) {
			first = it.cur->GetNodeContent();
		}
	}
	if (typeCount > 1) compoundFlag = true;

	// String / Number qualifiers (siblings of the <v8:Type> entries).
	wxXmlNode* strQual = FindChild(typeNode, strQualTag);
	if (strQual != nullptr) length = GetChildText(strQual, wxT("Length"));
	wxXmlNode* numQual = FindChild(typeNode, numQualTag);
	if (numQual != nullptr) {
		precision = GetChildText(numQual, wxT("Digits"));
		scale     = GetChildText(numQual, wxT("FractionDigits"));
	}

	wxString oes = MapTypeQualifier(first, refTarget);
	return oes;
}

// Build an attribute / dimension / resource property block as a JSON
// object — same shape `meta_create` expects for an Attribute child.
nlohmann::json BuildAttributeProperties(wxXmlNode* propsNode,
                                          std::vector<wxString>& warnings,
                                          const wxString& ownerName)
{
	nlohmann::json props = nlohmann::json::object();
	if (propsNode == nullptr) return props;

	const wxString name = GetChildText(propsNode, wxT("Name"));
	props["name"] = std::string(name.utf8_str().data());

	wxXmlNode* synNode = FindChild(propsNode, wxT("Synonym"));
	if (synNode != nullptr) props["synonym"] = ReadSynonymBlock(synNode);

	const wxString comment = GetChildText(propsNode, wxT("Comment"));
	if (!comment.empty()) {
		props["comment"] = std::string(comment.utf8_str().data());
	}

	wxString refTarget;
	bool     compound  = false;
	wxString length, precision, scale;
	const wxString oesType =
		ReadAttributeType(propsNode, refTarget, compound, length, precision, scale);

	if (oesType.empty()) {
		// Unknown type — fall back to String with a warning so the
		// import doesn't drop the attribute.
		warnings.push_back(wxString::Format(
			wxT("%s.%s: unrecognised type qualifier, falling back to String"),
			ownerName, name));
		props["type"] = "String";
	} else {
		props["type"] = std::string(oesType.utf8_str().data());
		if (!refTarget.empty()) {
			props["refType"] = std::string(refTarget.utf8_str().data());
		}
	}
	if (!length.empty()) {
		long n = 0;
		if (length.ToLong(&n)) props["length"] = static_cast<int>(n);
	}
	if (!precision.empty()) {
		long n = 0;
		if (precision.ToLong(&n)) props["precision"] = static_cast<int>(n);
	}
	if (!scale.empty()) {
		long n = 0;
		if (scale.ToLong(&n)) props["scale"] = static_cast<int>(n);
	}
	if (compound) {
		// Concatenate explicitly — wxString::Format with %s + wxString args
		// can mangle UTF-8 content when the format string carries non-ASCII
		// characters. Avoid em-dash entirely.
		wxString w;
		w << ownerName << wxT(".") << name
		  << wxT(": compound type - first non-deleted entry kept");
		warnings.push_back(w);
		props["compound"] = true;
	}
	return props;
}

// Helpers to emit a mutation entry. Mirrors the shape `oes_template_get`
// returns (op/kind/fullName/properties).
nlohmann::json MakeCreate(const wxString& kind, const wxString& fullName,
                            nlohmann::json properties)
{
	nlohmann::json m = nlohmann::json::object();
	m["op"]         = "create";
	m["kind"]       = std::string(kind.utf8_str().data());
	m["fullName"]   = std::string(fullName.utf8_str().data());
	m["properties"] = std::move(properties);
	return m;
}

// ----- per-kind dispatchers -----------------------------------------------

// EmitObjectMutation: write the root object's mutation entry + any direct
// scalar properties (Synonym, Comment, kind-specific bits). Returns the
// emitted property block so callers can inspect it for child enrichment.
void EmitObjectMutation(const wxString& oesKind, const wxString& fullName,
                          wxXmlNode* propsNode, ImportResult& out,
                          const wxString& basKind)
{
	nlohmann::json props = nlohmann::json::object();

	const wxString name = GetChildText(propsNode, wxT("Name"));
	props["name"] = std::string(name.utf8_str().data());

	wxXmlNode* synNode = FindChild(propsNode, wxT("Synonym"));
	if (synNode != nullptr) props["synonym"] = ReadSynonymBlock(synNode);

	const wxString comment = GetChildText(propsNode, wxT("Comment"));
	if (!comment.empty()) {
		props["comment"] = std::string(comment.utf8_str().data());
	}

	// Kind-specific properties.
	if (basKind == wxT("Catalog")) {
		const wxString hier = GetChildText(propsNode, wxT("Hierarchical"));
		if (hier == wxT("true")) props["hierarchical"] = true;
		const wxString codeLen = GetChildText(propsNode, wxT("CodeLength"));
		long ncl = 0; if (codeLen.ToLong(&ncl)) props["codeLength"] = static_cast<int>(ncl);
		const wxString descLen = GetChildText(propsNode, wxT("DescriptionLength"));
		long nd = 0; if (descLen.ToLong(&nd)) props["descriptionLength"] = static_cast<int>(nd);
	}
	else if (basKind == wxT("Document")) {
		const wxString numLen = GetChildText(propsNode, wxT("NumberLength"));
		long n = 0; if (numLen.ToLong(&n)) props["numberLength"] = static_cast<int>(n);
		const wxString numType = GetChildText(propsNode, wxT("NumberType"));
		if (!numType.empty()) props["numberType"] = std::string(numType.utf8_str().data());
		const wxString posting = GetChildText(propsNode, wxT("Posting"));
		if (!posting.empty()) props["posting"] = std::string(posting.utf8_str().data());
	}
	else if (basKind == wxT("InformationRegister")) {
		const wxString period = GetChildText(propsNode, wxT("InformationRegisterPeriodicity"));
		if (!period.empty()) props["periodicity"] = std::string(period.utf8_str().data());
		const wxString writeMode = GetChildText(propsNode, wxT("WriteMode"));
		if (!writeMode.empty()) props["writeMode"] = std::string(writeMode.utf8_str().data());
	}
	else if (basKind == wxT("AccumulationRegister")) {
		const wxString regType = GetChildText(propsNode, wxT("RegisterType"));
		if (!regType.empty()) props["registerType"] = std::string(regType.utf8_str().data());
	}

	out.mutations.push_back(MakeCreate(oesKind, fullName, std::move(props)));
}

// Walk ChildObjects of an object node, emitting Attribute / TabularSection /
// EnumValue / Resource / Dimension mutations as appropriate. Forms / Templates
// / Commands are stubbed with a deferred-form marker; modules are recorded as
// warnings (extraction from per-object subdir + .ext deferred to v2).
void EmitChildren(wxXmlNode* childObjectsNode, const wxString& oesKind,
                   const wxString& fullName, ImportResult& out)
{
	if (childObjectsNode == nullptr) return;

	for (wxXmlNode* c = childObjectsNode->GetChildren(); c != nullptr;
	     c = c->GetNext())
	{
		if (c->GetType() != wxXML_ELEMENT_NODE) continue;
		const wxString tag = LocalName(c);

		if (tag == wxT("Attribute")) {
			wxXmlNode* propsNode = FindChild(c, wxT("Properties"));
			if (propsNode == nullptr) continue;
			const wxString attrName = GetChildText(propsNode, wxT("Name"));
			nlohmann::json attrProps =
				BuildAttributeProperties(propsNode, out.warnings, fullName);
			const wxString attrFull = fullName + wxT(".Attributes.") + attrName;
			out.mutations.push_back(MakeCreate(wxT("Attribute"), attrFull,
			                                    std::move(attrProps)));
		}
		else if (tag == wxT("Resource") || tag == wxT("Dimension")) {
			wxXmlNode* propsNode = FindChild(c, wxT("Properties"));
			if (propsNode == nullptr) continue;
			const wxString childName = GetChildText(propsNode, wxT("Name"));
			nlohmann::json childProps =
				BuildAttributeProperties(propsNode, out.warnings, fullName);
			const wxString childKind = tag;  // "Resource" or "Dimension"
			const wxString childFull =
				fullName + wxT(".") + childKind + wxT("s.") + childName;
			out.mutations.push_back(MakeCreate(childKind, childFull,
			                                    std::move(childProps)));
		}
		else if (tag == wxT("EnumValue")) {
			wxXmlNode* propsNode = FindChild(c, wxT("Properties"));
			if (propsNode == nullptr) continue;
			const wxString evName = GetChildText(propsNode, wxT("Name"));
			nlohmann::json evProps = nlohmann::json::object();
			evProps["name"] = std::string(evName.utf8_str().data());
			wxXmlNode* synNode = FindChild(propsNode, wxT("Synonym"));
			if (synNode != nullptr) evProps["synonym"] = ReadSynonymBlock(synNode);
			const wxString ev_full = fullName + wxT(".Values.") + evName;
			out.mutations.push_back(MakeCreate(wxT("EnumValue"), ev_full,
			                                    std::move(evProps)));
		}
		else if (tag == wxT("TabularSection")) {
			wxXmlNode* propsNode = FindChild(c, wxT("Properties"));
			if (propsNode == nullptr) continue;
			const wxString tsName = GetChildText(propsNode, wxT("Name"));
			nlohmann::json tsProps = nlohmann::json::object();
			tsProps["name"] = std::string(tsName.utf8_str().data());
			wxXmlNode* synNode = FindChild(propsNode, wxT("Synonym"));
			if (synNode != nullptr) tsProps["synonym"] = ReadSynonymBlock(synNode);
			const wxString tsFull = fullName + wxT(".TabularSections.") + tsName;
			out.mutations.push_back(MakeCreate(wxT("TabularSection"), tsFull,
			                                    std::move(tsProps)));

			// Recurse into nested attributes.
			wxXmlNode* tsChildren = FindChild(c, wxT("ChildObjects"));
			EmitChildren(tsChildren, oesKind, tsFull, out);
		}
		else if (tag == wxT("Form")) {
			// BAS lists forms in ChildObjects as <Form>Name</Form> text
			// references; the actual layout lives under
			// <Name>/Forms/<Form>.xml as a binary form blob. The blob
			// format requires GUI-dependent control deserialisation
			// (t1-002), so v1 emits an empty-controls stub + warning.
			const wxString formName = c->GetNodeContent();
			if (formName.empty()) continue;
			nlohmann::json fProps = nlohmann::json::object();
			fProps["name"]     = std::string(formName.utf8_str().data());
			fProps["controls"] = nlohmann::json::array();
			fProps["deferred"] = true;
			const wxString fFull = fullName + wxT(".Forms.") + formName;
			out.mutations.push_back(MakeCreate(wxT("Form"), fFull,
			                                    std::move(fProps)));
			out.warnings.push_back(wxString::Format(
				wxT("%s: form controls deferred (form-blob deserialiser is GUI-dependent, t1-002)"),
				fFull));
		}
		else if (tag == wxT("Command")) {
			wxXmlNode* propsNode = FindChild(c, wxT("Properties"));
			if (propsNode == nullptr) continue;
			const wxString cmdName = GetChildText(propsNode, wxT("Name"));
			nlohmann::json cProps = nlohmann::json::object();
			cProps["name"] = std::string(cmdName.utf8_str().data());
			wxXmlNode* synNode = FindChild(propsNode, wxT("Synonym"));
			if (synNode != nullptr) cProps["synonym"] = ReadSynonymBlock(synNode);
			const wxString cFull = fullName + wxT(".Commands.") + cmdName;
			out.mutations.push_back(MakeCreate(wxT("Command"), cFull,
			                                    std::move(cProps)));
		}
		else if (tag == wxT("Template")) {
			// Templates (report layouts) deferred — log + skip.
			const wxString tplName = c->GetNodeContent();
			out.warnings.push_back(wxString::Format(
				wxT("%s.Templates.%s: report layout deferred (v2+)"),
				fullName, tplName));
		}
		// Else: skip silently — BAS scatters <Language> / <Subsystem> /
		// <StyleItem> entries in the root ChildObjects which we already
		// handle at the manifest level.
	}
}

// ----- single-file dispatch -----------------------------------------------

bool ImportObjectFromDocument(wxXmlDocument& doc, ImportResult& out)
{
	wxXmlNode* root = doc.GetRoot();
	if (root == nullptr) {
		++out.parseFailures;
		out.warnings.push_back(wxT("XML document has no root"));
		return false;
	}
	// Root element: <MetaDataObject><Kind uuid="..."><Properties>...
	// <ChildObjects>...</ChildObjects></Kind></MetaDataObject>
	if (LocalName(root) != wxT("MetaDataObject")) {
		++out.parseFailures;
		out.warnings.push_back(wxString::Format(
			wxT("root element is <%s>, expected <MetaDataObject>"),
			root->GetName()));
		return false;
	}

	// Find the first object-kind child (skips InternalInfo etc).
	wxXmlNode* kindNode = nullptr;
	for (wxXmlNode* n = root->GetChildren(); n != nullptr; n = n->GetNext()) {
		if (n->GetType() != wxXML_ELEMENT_NODE) continue;
		const wxString ln = LocalName(n);
		if (ln == wxT("InternalInfo")) continue;
		kindNode = n;
		break;
	}
	if (kindNode == nullptr) {
		++out.parseFailures;
		out.warnings.push_back(wxT("MetaDataObject has no kind child"));
		return false;
	}

	const wxString basKind = LocalName(kindNode);
	const KindMapping km   = MapKind(basKind);

	wxXmlNode* propsNode = FindChild(kindNode, wxT("Properties"));
	if (propsNode == nullptr) {
		++out.parseFailures;
		out.warnings.push_back(wxString::Format(
			wxT("<%s>: no <Properties> child"), basKind));
		return false;
	}
	const wxString objName = GetChildText(propsNode, wxT("Name"));
	if (objName.empty()) {
		++out.parseFailures;
		out.warnings.push_back(wxString::Format(
			wxT("<%s>: no <Name> in Properties"), basKind));
		return false;
	}

	// Dispatch on the kind status.
	if (km.status == KindStatus::Unknown) {
		++out.skippedUnknown;
		out.warnings.push_back(wxString::Format(
			wxT("%s.%s: unknown BAS kind, skipped"), basKind, objName));
		return false;
	}
	if (km.status == KindStatus::Deferred) {
		++out.skippedDeferred;
		out.warnings.push_back(wxString::Format(
			wxT("%s.%s: deferred - %s"), basKind, objName, km.reason));
		return false;
	}

	// Supported. Emit the root mutation + child mutations.
	const wxString fullName = km.oesKind + wxT(".") + objName;
	EmitObjectMutation(km.oesKind, fullName, propsNode, out, basKind);

	wxXmlNode* childObjectsNode = FindChild(kindNode, wxT("ChildObjects"));
	EmitChildren(childObjectsNode, km.oesKind, fullName, out);

	++out.imported;

	// Per-kind counter — linear scan over countsByKind is fine at v1 (~15 kinds).
	bool bumped = false;
	for (auto& kv : out.countsByKind) {
		if (kv.first == km.oesKind) { ++kv.second; bumped = true; break; }
	}
	if (!bumped) out.countsByKind.emplace_back(km.oesKind, 1);

	return true;
}

} // namespace

bool ImportSingleObjectFromText(const wxString& xmlText, ImportResult& out)
{
	++out.totalScanned;
	wxStringInputStream stream(xmlText);
	wxXmlDocument doc;
	{
		// Silence wxWidgets default logger for malformed XML — we already
		// surface the error through warnings.
		wxLogNull suppress;
		if (!doc.Load(stream)) {
			++out.parseFailures;
			out.warnings.push_back(wxT("malformed XML in synthetic text"));
			return false;
		}
	}
	return ImportObjectFromDocument(doc, out);
}

bool ImportSingleObjectFile(const wxString& path, ImportResult& out)
{
	++out.totalScanned;
	wxXmlDocument doc;
	{
		wxLogNull suppress;
		if (!doc.Load(path)) {
			++out.parseFailures;
			out.warnings.push_back(wxString::Format(
				wxT("failed to load XML: %s"), path));
			return false;
		}
	}
	return ImportObjectFromDocument(doc, out);
}

// ----- root configuration manifest -----------------------------------------

ImportResult ImportXmlConfiguration(const ImportOptions& opts)
{
	ImportResult res;

	// Pre-flight.
	if (opts.configurationPath.empty()) {
		res.fatal     = true;
		res.fatalCode = wxT("OES_E_BAS_INVALID_INPUT");
		res.fatalMsg  = wxT("configurationPath is required");
		return res;
	}
	if (!wxFileName::FileExists(opts.configurationPath)) {
		res.fatal     = true;
		res.fatalCode = wxT("OES_E_BAS_INVALID_INPUT");
		res.fatalMsg  = wxString::Format(
			wxT("Configuration.xml not found: %s"), opts.configurationPath);
		return res;
	}

	wxFileName rootFile(opts.configurationPath);
	const wxString objectsRoot =
		opts.objectsRoot.empty() ? rootFile.GetPath() : opts.objectsRoot;

	// Load the root Configuration.xml.
	wxXmlDocument doc;
	{
		wxLogNull suppress;
		if (!doc.Load(opts.configurationPath)) {
			res.fatal     = true;
			res.fatalCode = wxT("OES_E_BAS_PARSE_FAIL");
			res.fatalMsg  = wxString::Format(
				wxT("failed to parse Configuration.xml: %s"),
				opts.configurationPath);
			return res;
		}
	}

	wxXmlNode* root = doc.GetRoot();
	if (root == nullptr || LocalName(root) != wxT("MetaDataObject")) {
		res.fatal     = true;
		res.fatalCode = wxT("OES_E_BAS_PARSE_FAIL");
		res.fatalMsg  = wxT("Configuration.xml: root is not <MetaDataObject>");
		return res;
	}
	wxXmlNode* cfgNode = FindChild(root, wxT("Configuration"));
	if (cfgNode == nullptr) {
		res.fatal     = true;
		res.fatalCode = wxT("OES_E_BAS_PARSE_FAIL");
		res.fatalMsg  = wxT("Configuration.xml: no <Configuration> root");
		return res;
	}

	// Optional version detection — surface as a warning if it's a
	// version we haven't seen in the corpus (BAS 2.1 = 2.16 wire).
	const wxString version = root->GetAttribute(wxT("version"), wxT(""));
	if (!version.empty() && version != wxT("2.16") && version != wxT("2.15")) {
		res.warnings.push_back(wxString::Format(
			wxT("Configuration.xml: unfamiliar version '%s' (expected 2.15/2.16); proceeding"),
			version));
	}

	// Emit a Configuration-level mutation so the Applier can rename the
	// root config / set Vendor / Version. This isn't strictly required
	// but it surfaces useful BAS metadata.
	{
		wxXmlNode* cfgProps = FindChild(cfgNode, wxT("Properties"));
		if (cfgProps != nullptr) {
			const wxString cfgName = GetChildText(cfgProps, wxT("Name"));
			if (!cfgName.empty() && PassesFilter(wxT("Configuration.") + cfgName,
			                                      opts.filter))
			{
				nlohmann::json cprops = nlohmann::json::object();
				cprops["name"] = std::string(cfgName.utf8_str().data());
				wxXmlNode* synNode = FindChild(cfgProps, wxT("Synonym"));
				if (synNode != nullptr) cprops["synonym"] = ReadSynonymBlock(synNode);
				const wxString vendor = GetChildText(cfgProps, wxT("Vendor"));
				if (!vendor.empty()) cprops["vendor"] = std::string(vendor.utf8_str().data());
				const wxString cfgVer = GetChildText(cfgProps, wxT("Version"));
				if (!cfgVer.empty()) cprops["version"] = std::string(cfgVer.utf8_str().data());
				res.mutations.push_back(MakeCreate(
					wxT("Configuration"),
					wxT("Configuration.") + cfgName, std::move(cprops)));
			}
		}
	}

	// Walk ChildObjects: <Catalog>Name</Catalog>, <Document>Name</Document>...
	wxXmlNode* cfgChildren = FindChild(cfgNode, wxT("ChildObjects"));
	if (cfgChildren == nullptr) {
		res.warnings.push_back(wxT("Configuration.xml: empty <ChildObjects>"));
		return res;
	}

	for (wxXmlNode* n = cfgChildren->GetChildren(); n != nullptr; n = n->GetNext()) {
		if (n->GetType() != wxXML_ELEMENT_NODE) continue;
		const wxString basKind = LocalName(n);
		const wxString name    = n->GetNodeContent();
		if (name.empty()) continue;
		++res.totalScanned;

		// Legacy-deletion filter.
		if (opts.skipDeleted && IsLegacyDeletedName(name)) {
			++res.skippedDeleted;
			continue;
		}

		// Kind status check (early — saves opening files we'll skip).
		KindMapping km = MapKind(basKind);
		if (km.status == KindStatus::Unknown) {
			++res.skippedUnknown;
			res.warnings.push_back(wxString::Format(
				wxT("%s.%s: unknown BAS kind, skipped"), basKind, name));
			continue;
		}
		if (km.status == KindStatus::Deferred) {
			++res.skippedDeferred;
			// Don't spam warnings for every Subsystem / StyleItem; only the
			// first occurrence per kind.
			static thread_local std::map<wxString, bool> kSeen;
			if (!kSeen[basKind]) {
				kSeen[basKind] = true;
				res.warnings.push_back(wxString::Format(
					wxT("Kind '%s' deferred (%s) - %d+ occurrences skipped"),
					basKind, km.reason, 1));
			}
			continue;
		}

		// Apply caller's object filter.
		const wxString fullName = km.oesKind + wxT(".") + name;
		if (!PassesFilter(fullName, opts.filter)) {
			++res.skippedFiltered;
			continue;
		}

		// Resolve "<objectsRoot>/<KindDir>/<name>.xml".
		const wxString dir = DirectoryForBasKind(basKind);
		if (dir.empty()) {
			++res.skippedUnknown;
			res.warnings.push_back(wxString::Format(
				wxT("%s.%s: no directory mapping"), basKind, name));
			continue;
		}
		wxFileName f(objectsRoot, name + wxT(".xml"));
		f.AppendDir(dir);
		if (!f.FileExists()) {
			res.warnings.push_back(wxString::Format(
				wxT("%s.%s: per-object XML missing at %s"),
				basKind, name, f.GetFullPath()));
			++res.parseFailures;
			continue;
		}

		// Defer to the single-file path; it does the heavy lifting.
		ImportSingleObjectFile(f.GetFullPath(), res);
	}

	return res;
}

} // namespace bas
} // namespace migration
