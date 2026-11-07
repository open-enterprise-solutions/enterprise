#include "backend/metaCollection/metaGroups.h"

#include "backend/metaCollection/metaObject.h"   // the g_meta*CLSID constants
#include "core/stringUtils.h"                    // stringUtils::GenerateSynonym — a kind the table does not caption

namespace {

// ONE ROW PER GROUP, in the order an application is read. The row's POSITION is the order — there
// is no rank number to keep in step with the position, and none to leave a hole in when a kind is
// inserted between two others.
struct ibMetaGroupRow {
	ibClassID       m_clsid;
	// The SOURCE string, marked for extraction and left untranslated here: this table is static
	// data, built before a locale is loaded (the same rule as core/fileKind.cpp). Translating at
	// the point of use is what lets one table answer in whatever language the session runs in.
	const char*     m_caption;
	ibMetaGroupBand m_band;
	// What ONE metaobject of the kind is called ("Catalog" beside "Catalogs") — the kind a form's or a list's
	// caption names before the synonym. Marked the same way; empty where nothing is captioned by its kind.
	const char*     m_one = nullptr;
};

const ibMetaGroupRow s_groups[] = {

	// ——— what belongs to the application as a whole and to no business object ———
	{ g_metaSharedModuleCLSID,     ibTRANSLATE("Shared modules"),     ibMetaGroupBand::Shared },
	{ g_metaSharedFormCLSID,       ibTRANSLATE("Shared forms"),       ibMetaGroupBand::Shared },
	{ g_metaSharedCommandCLSID,    ibTRANSLATE("Shared commands"),    ibMetaGroupBand::Shared },
	{ g_metaCommandGroupCLSID,     ibTRANSLATE("Command groups"),     ibMetaGroupBand::Shared },
	{ g_metaSharedTemplateCLSID,   ibTRANSLATE("Shared templates"),   ibMetaGroupBand::Shared },
	// The jobs branch holds the parameterized ones; the predefined sit in a sub-branch of it, so
	// they are declared right after it.
	{ g_metaParameterizedJobCLSID, ibTRANSLATE("Scheduled jobs"),     ibMetaGroupBand::Shared },
	{ g_metaScheduledJobCLSID,     ibTRANSLATE("Predefined jobs"),    ibMetaGroupBand::Shared },
	{ g_metaEventHandlerCLSID,     ibTRANSLATE("Event handlers"),     ibMetaGroupBand::Shared },
	{ g_metaSessionParameterCLSID, ibTRANSLATE("Session parameters"), ibMetaGroupBand::Shared },
	{ g_metaFeatureOptionCLSID,    ibTRANSLATE("Feature options"),    ibMetaGroupBand::Shared, ibTRANSLATE("Feature option") },
	{ g_metaSharedAttributeCLSID,  ibTRANSLATE("Shared attributes"),  ibMetaGroupBand::Shared },
	{ g_metaPictureCLSID,          ibTRANSLATE("Pictures"),           ibMetaGroupBand::Shared },
	{ g_metaSectionCLSID,          ibTRANSLATE("Sections"),           ibMetaGroupBand::Shared },
	{ g_metaRoleCLSID,             ibTRANSLATE("Roles"),              ibMetaGroupBand::Shared },
	{ g_metaLanguageCLSID,         ibTRANSLATE("Languages"),          ibMetaGroupBand::Shared },

	// ——— straight under the application: first what another system talks to the base through — a folder of its own
	// right after Common, its groups after it — then the business objects an application is made of ———
	{ g_metaIntegrationCLSID,                ibTRANSLATE("Integration"),     ibMetaGroupBand::Metadata },
	{ g_metaHttpServiceCLSID,                ibTRANSLATE("HTTP services"),   ibMetaGroupBand::Metadata },
	{ g_metaConstantCLSID,                  ibTRANSLATE("Constants"),       ibMetaGroupBand::Metadata, ibTRANSLATE("Constant") },
	{ g_metaCatalogCLSID,                    ibTRANSLATE("Catalogs"),        ibMetaGroupBand::Metadata, ibTRANSLATE("Catalog") },
	{ g_metaDocumentCLSID,                   ibTRANSLATE("Documents"),       ibMetaGroupBand::Metadata, ibTRANSLATE("Document") },
	// A journal lists the documents above it, so it stands right under them.
	{ g_metaDocumentJournalCLSID,            ibTRANSLATE("Document journals"), ibMetaGroupBand::Metadata, ibTRANSLATE("Document journal") },
	// …and the number series documents may share, right after them: shown as a branch of Documents.
	{ g_metaDocumentNumeratorCLSID,          ibTRANSLATE("Document numerators"), ibMetaGroupBand::Metadata, ibTRANSLATE("Document numerator") },
	{ g_metaEnumerationCLSID,                ibTRANSLATE("Enumerations"),    ibMetaGroupBand::Metadata, ibTRANSLATE("Enumeration") },
	{ g_metaDataToolCLSID,                   ibTRANSLATE("Data tools"),      ibMetaGroupBand::Metadata, ibTRANSLATE("Data tool") },
	{ g_metaReportCLSID,                     ibTRANSLATE("Reports"),         ibMetaGroupBand::Metadata, ibTRANSLATE("Report") },
	// ⭐ THE CHARTS BEFORE THE REGISTERS, AND THE REGISTERS LAST — see the header.
	{ g_metaChartOfCharacteristicTypesCLSID, ibTRANSLATE("Charts of characteristic types"), ibMetaGroupBand::Metadata, ibTRANSLATE("Chart of characteristic types") },
	{ g_metaChartOfAccountsCLSID,            ibTRANSLATE("Charts of accounts"),             ibMetaGroupBand::Metadata, ibTRANSLATE("Chart of accounts") },
	{ g_metaChartOfCalculationTypesCLSID,    ibTRANSLATE("Charts of calculation types"),    ibMetaGroupBand::Metadata, ibTRANSLATE("Chart of calculation types") },
	{ g_metaInformationRegisterCLSID,        ibTRANSLATE("Information registers"),          ibMetaGroupBand::Metadata, ibTRANSLATE("Information register") },
	{ g_metaAccumulationRegisterCLSID,       ibTRANSLATE("Accumulation registers"),         ibMetaGroupBand::Metadata, ibTRANSLATE("Accumulation register") },
	{ g_metaAccountingRegisterCLSID,         ibTRANSLATE("Accounting registers"),           ibMetaGroupBand::Metadata, ibTRANSLATE("Accounting register") },
	{ g_metaCalculationRegisterCLSID,        ibTRANSLATE("Calculation registers"),          ibMetaGroupBand::Metadata, ibTRANSLATE("Calculation register") },
	// …and the sequences last of all: a sequence is about the order the DOCUMENTS above were posted
	// in, so it is read after everything it is about (sequence-arc.md).
	{ g_metaSequenceCLSID,                   ibTRANSLATE("Sequences"),                      ibMetaGroupBand::Metadata, ibTRANSLATE("Sequence") },

	// ——— the groups INSIDE an object: what it IS, then what is done with it ———
	{ g_metaAttributeCLSID,                      ibTRANSLATE("Attributes"),  ibMetaGroupBand::Inner },
	// A shared attribute's copy inside an object compares under the same caption as the declaration:
	// what a reader wants to see there is "this object carries a shared attribute", not a second
	// kind of thing.
	{ g_metaSharedAttributeColumnCLSID,          ibTRANSLATE("Shared attributes"), ibMetaGroupBand::Inner },
	{ g_metaDimensionCLSID,                      ibTRANSLATE("Dimensions"),  ibMetaGroupBand::Inner },
	{ g_metaResourceCLSID,                       ibTRANSLATE("Resources"),   ibMetaGroupBand::Inner },
	{ g_metaEnumCLSID,                           ibTRANSLATE("Enum values"), ibMetaGroupBand::Inner },
	// A chart's kinds of accounting stand right after its attributes — where a person looks for
	// them, and in the order the accounting world reads: what the ACCOUNT is kept in, then what
	// each of its breakdowns is.
	{ g_metaAccountingKindCLSID,                 ibTRANSLATE("Accounting kinds"), ibMetaGroupBand::Inner },
	{ g_metaAccountDimensionAccountingKindCLSID, ibTRANSLATE("Account dimension accounting kinds"), ibMetaGroupBand::Inner },
	{ g_metaPredefinedAttributeCLSID,            ibTRANSLATE("Predefined attributes"), ibMetaGroupBand::Inner },
	{ g_metaTableCLSID,                          ibTRANSLATE("Tables"),      ibMetaGroupBand::Inner },
	{ g_metaTableRefCLSID,                       ibTRANSLATE("Tables"),      ibMetaGroupBand::Inner },
	{ g_metaAccountDimensionKindsTableCLSID,     ibTRANSLATE("Account dimension kinds tables"), ibMetaGroupBand::Inner },
	{ g_metaFormCLSID,                           ibTRANSLATE("Forms"),       ibMetaGroupBand::Inner },
	{ g_metaCommandCLSID,                        ibTRANSLATE("Commands"),    ibMetaGroupBand::Inner },
	{ g_metaTemplateCLSID,                       ibTRANSLATE("Templates"),   ibMetaGroupBand::Inner },
	// …and a service's: its URL templates, each with its methods.
	{ g_metaUrlTemplateCLSID,                    ibTRANSLATE("URL templates"), ibMetaGroupBand::Inner },
	{ g_metaHttpServiceMethodCLSID,              ibTRANSLATE("Methods"),     ibMetaGroupBand::Inner },
	{ g_metaModuleCLSID,                         ibTRANSLATE("Modules"),     ibMetaGroupBand::Inner },
	{ g_metaManagerCLSID,                        ibTRANSLATE("Manager modules"), ibMetaGroupBand::Inner },
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

ibString ibMetaGroupCaption(const ibClassID& clsid)
{
	const ibMetaGroupRow* row = RowFor(clsid);
	return row != nullptr ? ibGetTranslation(ibString::FromUTF8(row->m_caption)) : ibString();
}

ibString ibMetaTypeCaption(const ibClassID& clsid)
{
	// A kind this table does not caption — an external data tool, a plugin's — is called by the name it
	// registered, spelled as words and translated where the catalog has it ("External report").
	const ibMetaGroupRow* row = RowFor(clsid);
	return ibGetTranslation(row != nullptr && row->m_one != nullptr ? ibString::FromUTF8(row->m_one)
		: stringUtils::GenerateSynonym(ibValue::GetNameObjectFromID(clsid)));
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
