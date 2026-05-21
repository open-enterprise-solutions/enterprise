/////////////////////////////////////////////////////////////////////////////
// basMapping — see header.
//
// Lookup tables are intentionally small and explicit; we don't pull in
// a hash map for ~15 entries. Linear scan is fine and keeps the binary
// surface flat.
/////////////////////////////////////////////////////////////////////////////

#include "basMapping.hpp"

namespace migration {
namespace bas {
namespace {

struct KindRow {
	const wxChar* basKind;
	KindStatus    status;
	const wxChar* oesKind;     // empty for Deferred
	const wxChar* reason;      // populated only on Deferred — explains why
};

// Anchored on the spec's mapping table.  Top-8 OES-supported kinds first,
// then the deferred / out-of-scope kinds that BAS commonly ships. Anything
// not listed falls through to KindStatus::Unknown.
const KindRow kKindTable[] = {
	// Direct equivalents
	{ wxT("Catalog"),                    KindStatus::Supported, wxT("Catalog"),                    wxT("") },
	{ wxT("Document"),                   KindStatus::Supported, wxT("Document"),                   wxT("") },
	{ wxT("Enum"),                       KindStatus::Supported, wxT("Enumeration"),                wxT("") },
	{ wxT("Constant"),                   KindStatus::Supported, wxT("Constant"),                   wxT("") },
	{ wxT("InformationRegister"),        KindStatus::Supported, wxT("InformationRegister"),        wxT("") },
	{ wxT("AccumulationRegister"),       KindStatus::Supported, wxT("AccumulationRegister"),       wxT("") },
	{ wxT("AccountingRegister"),         KindStatus::Supported, wxT("AccountingRegister"),         wxT("") },
	{ wxT("ChartOfAccounts"),            KindStatus::Supported, wxT("ChartOfAccounts"),            wxT("") },
	{ wxT("ChartOfCharacteristicTypes"), KindStatus::Supported, wxT("ChartOfCharacteristicTypes"), wxT("") },
	{ wxT("Report"),                     KindStatus::Supported, wxT("Report"),                     wxT("") },
	{ wxT("DataProcessor"),              KindStatus::Supported, wxT("DataProcessor"),              wxT("") },
	{ wxT("CommonModule"),               KindStatus::Supported, wxT("CommonModule"),               wxT("") },

	// Deferred — has BAS analogue but OES doesn't expose a canonical kind yet.
	{ wxT("Subsystem"),         KindStatus::Deferred, wxT(""), wxT("OES has no canonical Subsystem kind yet (v2+)") },
	{ wxT("Role"),              KindStatus::Deferred, wxT(""), wxT("RBAC mapping is platform-specific (v2+)") },
	{ wxT("CommonForm"),        KindStatus::Deferred, wxT(""), wxT("Form blob serializer GUI-dependent (t1-002)") },
	{ wxT("CommonCommand"),     KindStatus::Deferred, wxT(""), wxT("Command UI binding pending Subsystem support") },
	{ wxT("CommonTemplate"),    KindStatus::Deferred, wxT(""), wxT("Report layouts complex — v2+") },
	{ wxT("CommonPicture"),     KindStatus::Deferred, wxT(""), wxT("Binary asset handling — v2+") },
	{ wxT("CommonAttribute"),   KindStatus::Deferred, wxT(""), wxT("Cross-object attribute joins — v2+") },
	{ wxT("DefinedType"),       KindStatus::Deferred, wxT(""), wxT("OES type alias system pending") },
	{ wxT("ExchangePlan"),      KindStatus::Deferred, wxT(""), wxT("Replication subsystem absent in OES") },
	{ wxT("DocumentJournal"),   KindStatus::Deferred, wxT(""), wxT("Cross-doc journals pending") },
	{ wxT("DocumentNumerator"), KindStatus::Deferred, wxT(""), wxT("Numbering scheme absent in OES") },
	{ wxT("FunctionalOption"),  KindStatus::Deferred, wxT(""), wxT("Feature-flag subsystem absent") },
	{ wxT("FunctionalOptionsParameter"), KindStatus::Deferred, wxT(""), wxT("Functional option dependency") },
	{ wxT("EventSubscription"), KindStatus::Deferred, wxT(""), wxT("Event-handler subsystem absent") },
	{ wxT("ScheduledJob"),      KindStatus::Deferred, wxT(""), wxT("Background job scheduler absent") },
	{ wxT("SettingsStorage"),   KindStatus::Deferred, wxT(""), wxT("User-settings persistence absent") },
	{ wxT("StyleItem"),         KindStatus::Deferred, wxT(""), wxT("Style / theming pending") },
	{ wxT("WebService"),        KindStatus::Deferred, wxT(""), wxT("SOAP endpoints absent") },
	{ wxT("HTTPService"),       KindStatus::Deferred, wxT(""), wxT("HTTP endpoint subsystem absent") },
	{ wxT("WSReference"),       KindStatus::Deferred, wxT(""), wxT("SOAP client absent") },
	{ wxT("XDTOPackage"),       KindStatus::Deferred, wxT(""), wxT("XDTO type system absent") },
	{ wxT("BusinessProcess"),   KindStatus::Deferred, wxT(""), wxT("Process modelling subsystem absent") },
	{ wxT("Task"),              KindStatus::Deferred, wxT(""), wxT("Task subsystem absent") },
	{ wxT("FilterCriterion"),   KindStatus::Deferred, wxT(""), wxT("Saved-filter subsystem absent") },
	{ wxT("ChartOfCalculationTypes"), KindStatus::Deferred, wxT(""), wxT("Payroll calculation subsystem absent") },
	{ wxT("CalculationRegister"),     KindStatus::Deferred, wxT(""), wxT("Payroll calculation subsystem absent") },
	{ wxT("CommandGroup"),      KindStatus::Deferred, wxT(""), wxT("Command-grouping needs Subsystem") },
	{ wxT("Language"),          KindStatus::Deferred, wxT(""), wxT("Locale catalogue managed via OES install") },
	{ wxT("SessionParameter"),  KindStatus::Deferred, wxT(""), wxT("Per-session params absent") },
};

constexpr std::size_t kKindTableSize = sizeof(kKindTable) / sizeof(kKindTable[0]);

} // namespace

KindMapping MapKind(const wxString& basKind)
{
	for (std::size_t i = 0; i < kKindTableSize; ++i) {
		if (basKind == kKindTable[i].basKind) {
			KindMapping m;
			m.status  = kKindTable[i].status;
			m.oesKind = kKindTable[i].oesKind;
			m.reason  = kKindTable[i].reason;
			return m;
		}
	}
	KindMapping m;
	m.status = KindStatus::Unknown;
	return m;
}

wxString MapTypeQualifier(const wxString& basType, wxString& refTarget)
{
	refTarget.Clear();

	// Strip optional cfg: / v8: prefixes — the corpus uses
	// <v8:Type>cfg:CatalogRef.X</v8:Type> for reference types, and
	// <v8:Type>xs:string</v8:Type> for primitives. The reader passes us
	// just the body text; we accept either prefix or none.
	wxString t = basType;
	if (t.StartsWith(wxT("cfg:")))     t = t.Mid(4);
	else if (t.StartsWith(wxT("xs:"))) t = t.Mid(3);
	else if (t.StartsWith(wxT("v8:"))) t = t.Mid(3);

	// Primitives.
	if (t == wxT("string"))  return wxT("String");
	if (t == wxT("decimal")) return wxT("Number");
	if (t == wxT("dateTime"))return wxT("Date");
	if (t == wxT("date"))    return wxT("Date");
	if (t == wxT("boolean")) return wxT("Boolean");
	if (t == wxT("UUID"))    return wxT("String");   // marker on caller side

	// Reference variants. Pattern: "<Kind>Ref.<Name>".
	struct RefRow { const wxChar* prefix; const wxChar* oesKind; };
	static const RefRow kRefs[] = {
		{ wxT("CatalogRef."),                    wxT("Catalog") },
		{ wxT("DocumentRef."),                   wxT("Document") },
		{ wxT("EnumRef."),                       wxT("Enumeration") },
		{ wxT("ChartOfAccountsRef."),            wxT("ChartOfAccounts") },
		{ wxT("ChartOfCharacteristicTypesRef."), wxT("ChartOfCharacteristicTypes") },
		{ wxT("ExchangePlanRef."),               wxT("ExchangePlan") },   // deferred but recognise
		{ wxT("BusinessProcessRef."),            wxT("BusinessProcess") },
		{ wxT("TaskRef."),                       wxT("Task") },
		{ wxT("DefinedType."),                   wxT("DefinedType") },
	};
	constexpr std::size_t kRefCount = sizeof(kRefs) / sizeof(kRefs[0]);
	for (std::size_t i = 0; i < kRefCount; ++i) {
		const wxString prefix(kRefs[i].prefix);
		if (t.StartsWith(prefix)) {
			refTarget = wxString(kRefs[i].oesKind);
			refTarget << wxT(".") << t.Mid(prefix.length());
			return wxT("Reference");
		}
	}

	// Generic Ref / AnyRef fall back to a typed reference but with no specific
	// target — surfaces as a warning in the caller.
	if (t == wxT("AnyRef") || t == wxT("Ref")) {
		refTarget = wxT("Catalog.Any");
		return wxT("Reference");
	}

	// Unknown — caller decides whether to fall back to String + warning.
	return wxString();
}

bool IsLegacyDeletedName(const wxString& name)
{
	// BAS uses both "Удалить..." (Russian) and the uppercase "УДАЛИТЬ..."
	// to mark migration-debt objects. The corpus's analysis report counts
	// ~80 of these. Match case-sensitively against the two known forms.
	if (name.StartsWith(wxT("Удалить")) || name.StartsWith(wxT("УДАЛИТЬ"))) {
		return true;
	}
	// UA-localised variant occasionally seen in BAS Бухгалтерія.
	if (name.StartsWith(wxT("Видалити")) || name.StartsWith(wxT("ВИДАЛИТИ"))) {
		return true;
	}
	return false;
}

} // namespace bas
} // namespace migration
