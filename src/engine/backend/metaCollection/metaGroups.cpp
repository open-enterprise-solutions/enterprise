#include "backend/metaCollection/metaGroups.h"

#include "backend/metaCollection/metaObject.h"   // the g_meta*CLSID constants

#include <wx/intl.h>

namespace {

// ONE ROW PER GROUP, in the order a configuration is read. The row's POSITION is the order — there
// is no rank number to keep in step with the position, and none to leave a hole in when a kind is
// inserted between two others.
struct ibMetaGroupRow {
	ibClassID       m_clsid;
	// The SOURCE string, marked for extraction and left untranslated here: this table is static
	// data, built before a locale is loaded (the same rule as backend/fileKind.cpp). Translating at
	// the point of use is what lets one table answer in whatever language the session runs in.
	const char*     m_caption;
	ibMetaGroupBand m_band;
};

const ibMetaGroupRow s_groups[] = {

	// ——— what belongs to the configuration as a whole and to no business object ———
	{ g_metaCommonModuleCLSID,     wxTRANSLATE("Common modules"),     ibMetaGroupBand::Common },
	{ g_metaCommonFormCLSID,       wxTRANSLATE("Common forms"),       ibMetaGroupBand::Common },
	{ g_metaCommonCommandCLSID,    wxTRANSLATE("Common commands"),    ibMetaGroupBand::Common },
	{ g_metaCommonTemplateCLSID,   wxTRANSLATE("Common templates"),   ibMetaGroupBand::Common },
	// The jobs branch holds the parameterized ones; the predefined sit in a sub-branch of it, so
	// they are declared right after it.
	{ g_metaParameterizedJobCLSID, wxTRANSLATE("Scheduled jobs"),     ibMetaGroupBand::Common },
	{ g_metaScheduledJobCLSID,     wxTRANSLATE("Predefined jobs"),    ibMetaGroupBand::Common },
	{ g_metaSessionParameterCLSID, wxTRANSLATE("Session parameters"), ibMetaGroupBand::Common },
	{ g_metaCommonAttributeCLSID,  wxTRANSLATE("Common attributes"),  ibMetaGroupBand::Common },
	{ g_metaPictureCLSID,          wxTRANSLATE("Pictures"),           ibMetaGroupBand::Common },
	{ g_metaSectionCLSID,          wxTRANSLATE("Sections"),           ibMetaGroupBand::Common },
	{ g_metaRoleCLSID,             wxTRANSLATE("Roles"),              ibMetaGroupBand::Common },
	{ g_metaLanguageCLSID,         wxTRANSLATE("Languages"),          ibMetaGroupBand::Common },

	// ——— the business objects a configuration is made of ———
	{ g_metaConstantCLSID,                   wxTRANSLATE("Constants"),       ibMetaGroupBand::Metadata },
	{ g_metaCatalogCLSID,                    wxTRANSLATE("Catalogs"),        ibMetaGroupBand::Metadata },
	{ g_metaDocumentCLSID,                   wxTRANSLATE("Documents"),       ibMetaGroupBand::Metadata },
	{ g_metaEnumerationCLSID,                wxTRANSLATE("Enumerations"),    ibMetaGroupBand::Metadata },
	{ g_metaDataProcessorCLSID,              wxTRANSLATE("Data processors"), ibMetaGroupBand::Metadata },
	{ g_metaReportCLSID,                     wxTRANSLATE("Reports"),         ibMetaGroupBand::Metadata },
	// ⭐ THE CHARTS BEFORE THE REGISTERS, AND THE REGISTERS LAST — see the header.
	{ g_metaChartOfCharacteristicTypesCLSID, wxTRANSLATE("Charts of characteristic types"), ibMetaGroupBand::Metadata },
	{ g_metaChartOfAccountsCLSID,            wxTRANSLATE("Charts of accounts"),             ibMetaGroupBand::Metadata },
	{ g_metaChartOfCalculationTypesCLSID,    wxTRANSLATE("Charts of calculation types"),    ibMetaGroupBand::Metadata },
	{ g_metaInformationRegisterCLSID,        wxTRANSLATE("Information registers"),          ibMetaGroupBand::Metadata },
	{ g_metaAccumulationRegisterCLSID,       wxTRANSLATE("Accumulation registers"),         ibMetaGroupBand::Metadata },
	{ g_metaAccountingRegisterCLSID,         wxTRANSLATE("Accounting registers"),           ibMetaGroupBand::Metadata },
	{ g_metaCalculationRegisterCLSID,        wxTRANSLATE("Calculation registers"),          ibMetaGroupBand::Metadata },
	// …and the sequences last of all: a sequence is about the order the DOCUMENTS above were posted
	// in, so it is read after everything it is about (sequence-arc.md).
	{ g_metaSequenceCLSID,                   wxTRANSLATE("Sequences"),                      ibMetaGroupBand::Metadata },

	// ——— the groups INSIDE an object: what it IS, then what is done with it ———
	{ g_metaAttributeCLSID,                      wxTRANSLATE("Attributes"),  ibMetaGroupBand::Inner },
	// A common attribute's copy inside an object compares under the same caption as the declaration:
	// what a reader wants to see there is "this object carries a common attribute", not a second
	// kind of thing.
	{ g_metaCommonAttributeColumnCLSID,          wxTRANSLATE("Common attributes"), ibMetaGroupBand::Inner },
	{ g_metaDimensionCLSID,                      wxTRANSLATE("Dimensions"),  ibMetaGroupBand::Inner },
	{ g_metaResourceCLSID,                       wxTRANSLATE("Resources"),   ibMetaGroupBand::Inner },
	{ g_metaEnumCLSID,                           wxTRANSLATE("Enum values"), ibMetaGroupBand::Inner },
	// A chart's kinds of accounting stand right after its attributes — where a person looks for
	// them, and in the order the accounting world reads: what the ACCOUNT is kept in, then what
	// each of its breakdowns is.
	{ g_metaAccountingKindCLSID,                 wxTRANSLATE("Accounting kinds"), ibMetaGroupBand::Inner },
	{ g_metaAccountDimensionAccountingKindCLSID, wxTRANSLATE("Account dimension accounting kinds"), ibMetaGroupBand::Inner },
	{ g_metaPredefinedAttributeCLSID,            wxTRANSLATE("Predefined attributes"), ibMetaGroupBand::Inner },
	{ g_metaTableCLSID,                          wxTRANSLATE("Tables"),      ibMetaGroupBand::Inner },
	{ g_metaTableRefCLSID,                       wxTRANSLATE("Tables"),      ibMetaGroupBand::Inner },
	{ g_metaAccountDimensionKindsTableCLSID,     wxTRANSLATE("Account dimension kinds tables"), ibMetaGroupBand::Inner },
	{ g_metaRecalculationCLSID,                  wxTRANSLATE("Recalculations"), ibMetaGroupBand::Inner },
	{ g_metaFormCLSID,                           wxTRANSLATE("Forms"),       ibMetaGroupBand::Inner },
	{ g_metaCommandCLSID,                        wxTRANSLATE("Commands"),    ibMetaGroupBand::Inner },
	{ g_metaTemplateCLSID,                       wxTRANSLATE("Templates"),   ibMetaGroupBand::Inner },
	{ g_metaComposerCLSID,                       wxTRANSLATE("Composers"),   ibMetaGroupBand::Inner },
	{ g_metaModuleCLSID,                         wxTRANSLATE("Modules"),     ibMetaGroupBand::Inner },
	{ g_metaManagerCLSID,                        wxTRANSLATE("Manager modules"), ibMetaGroupBand::Inner },
};

const ibMetaGroupRow* RowFor(const ibClassID& clsid)
{
	for (const ibMetaGroupRow& row : s_groups) {
		if (row.m_clsid == clsid)
			return &row;
	}
	return nullptr;   // a kind this table does not name — a plugin's, or one nobody has placed yet
}

} // namespace

wxString ibMetaGroupCaption(const ibClassID& clsid)
{
	const ibMetaGroupRow* row = RowFor(clsid);
	return row != nullptr ? wxGetTranslation(wxString::FromUTF8(row->m_caption)) : wxString();
}

int ibMetaGroupOrder(const ibClassID& clsid)
{
	// The position IS the order, and the count is what an unnamed kind answers: it sorts after every
	// declared one instead of ahead of all of them.
	int at = 0;
	for (const ibMetaGroupRow& row : s_groups) {
		if (row.m_clsid == clsid)
			return at;
		++at;
	}
	return at;
}

ibMetaGroupBand ibMetaGroupBandOf(const ibClassID& clsid)
{
	const ibMetaGroupRow* row = RowFor(clsid);
	return row != nullptr ? row->m_band : ibMetaGroupBand::Inner;
}
