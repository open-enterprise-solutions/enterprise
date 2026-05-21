/////////////////////////////////////////////////////////////////////////////
// basMapping — BAS / 1С XML kind + type qualifier mapping to OES.
//
// The BAS Configuration.xml dialect (used by 1С Predприятие 8.x AND BAS
// Бухгалтерія 2.1 UA) names objects in English (Catalog, Document, ...)
// even though synonyms / actual identifier names are in Russian or
// Ukrainian. We only need to translate the *kind* tag — names pass
// through verbatim.
//
// Returned strings are owned by the lookup tables (string literals). Do
// not free.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_BAS_MAPPING_HPP_
#define _IB_BAS_MAPPING_HPP_

#include <wx/string.h>

namespace migration {
namespace bas {

// Outcome of mapping a BAS root-object kind to an OES kind.
enum class KindStatus {
	Supported,   // direct OES equivalent — mutation will be emitted
	Deferred,    // OES does not have a canonical equivalent yet (v2+)
	Unknown      // unrecognised kind — log warning, skip
};

struct KindMapping {
	KindStatus status;
	// Populated only when status == Supported: the OES kind name fed to
	// metaBridge / oes_template_customize mutations[].op="create".
	wxString   oesKind;
	// Optional deferral rationale — surfaced in warnings[].
	wxString   reason;
};

// Map a top-level BAS kind tag (root XML element name inside MetaDataObject,
// e.g. "Catalog", "Document", "Enum", "InformationRegister") to OES.
KindMapping MapKind(const wxString& basKind);

// Map a BAS XML type qualifier (the body of <Type><v8:Type>...</v8:Type>
// </Type>) to the OES Attribute.type representation. Returns the OES type
// label (e.g. "String", "Reference"); when the BAS type is a Ref form
// (CatalogRef.X / DocumentRef.X / EnumRef.X / ChartOfAccountsRef.X) the
// out parameter `refTarget` is populated with the OES full-name reference
// target (e.g. "Catalog.X"). For primitive types refTarget is left empty.
//
// Returns empty wxString when the type is unrecognised — caller should
// emit a warning and either fall back to String or skip the attribute.
wxString MapTypeQualifier(const wxString& basType, wxString& refTarget);

// True when the BAS object name matches the legacy "Удалить..." or
// "УДАЛИТЬ..." prefix (BAS's migration-debt marker). Used by the parser
// to honour skipDeleted=true.
bool IsLegacyDeletedName(const wxString& name);

} // namespace bas
} // namespace migration

#endif // _IB_BAS_MAPPING_HPP_
