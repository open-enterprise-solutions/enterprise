////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-access 
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "metaObjectCollection.h"
#include "backend/metaData.h"

#include <iterator>   // std::size — the table of kinds

// Metadata.Catalogs, … — the metaobjects of one kind themselves (metaObjectCollection.h).
class ibValueMetadataCollection : public ibValueMetaObjectCollection {
public:

	ibValueMetadataCollection() : ibValueMetaObjectCollection(nullptr) {}
	ibValueMetadataCollection(const ibClassID& clsid, ibMetaData* metaData) : ibValueMetaObjectCollection(metaData) {
		m_listValue = m_metaData->GetAnyArrayObject(clsid);
	}

protected:

	virtual ibValue ElementValue(ibValueMetaObject* const& object) const override {
		return object != nullptr ? ibValue(object) : ibValue();
	}
};

namespace {

// THE KINDS, one per member, in the members' order: the name a script reads and the kind it stands for. A member's
// number is its position here, so FillMembers and GetPropVal read one table.
struct ibMetadataKind {
	const ibChar* m_name;
	ibClassID     m_clsid;
};

constexpr ibMetadataKind s_metadataKinds[] = {
	{ ibT("CommonModules"),               g_metaCommonModuleCLSID },
	{ ibT("CommonForms"),                 g_metaCommonFormCLSID },
	{ ibT("CommonTemplates"),             g_metaCommonTemplateCLSID },
	{ ibT("Constants"),                   g_metaConstantCLSID },
	{ ibT("Catalogs"),                    g_metaCatalogCLSID },
	{ ibT("Documents"),                   g_metaDocumentCLSID },
	{ ibT("Enumerations"),                g_metaEnumerationCLSID },
	{ ibT("DataProcessors"),              g_metaDataProcessorCLSID },
	{ ibT("Reports"),                     g_metaReportCLSID },
	{ ibT("InformationRegisters"),        g_metaInformationRegisterCLSID },
	{ ibT("AccumulationRegisters"),       g_metaAccumulationRegisterCLSID },
	{ ibT("ChartsOfCharacteristicTypes"), g_metaChartOfCharacteristicTypesCLSID },
	{ ibT("ChartsOfAccounts"),            g_metaChartOfAccountsCLSID },
	{ ibT("AccountingRegisters"),         g_metaAccountingRegisterCLSID },
	{ ibT("ChartsOfCalculationTypes"),    g_metaChartOfCalculationTypesCLSID },
	{ ibT("CalculationRegisters"),        g_metaCalculationRegisterCLSID },
	{ ibT("Sequences"),                   g_metaSequenceCLSID },
};

} // namespace

ibValueModuleManager::ibValueMetadataUnit::ibValueMetadataUnit(ibMetaData* metaData) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_metaData(metaData)
{
	m_members.Bind(this, &ibValueMetadataUnit::FillMembers);
}

ibValueModuleManager::ibValueMetadataUnit::~ibValueMetadataUnit()
{
}

void ibValueModuleManager::ibValueMetadataUnit::FillMembers(ibMemberTable& helper) const
{
	// The clsid rides in the (long) property-tag slot only to distinguish these members; GetPropVal
	// dispatches by ordinal and re-supplies the clsid itself, so the narrowing is intentional and
	// lossless-in-practice. static_cast makes it explicit (silences C4305/C4309 on the std::uint64_t clsid).
	for (const ibMetadataKind& kind : s_metadataKinds)
		helper.AppendProp(kind.m_name, true, false, static_cast<long>(kind.m_clsid));
}

//****************************************************************************
//*                              Override attribute                          *
//****************************************************************************

bool ibValueModuleManager::ibValueMetadataUnit::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueModuleManager::ibValueMetadataUnit::GetPropVal(const long lPropNum, ibValue& pvarPropVal)//attribute value
{
	if (lPropNum < 0 || lPropNum >= (long)std::size(s_metadataKinds))
		return false;
	pvarPropVal = new ibValueMetadataCollection(s_metadataKinds[lPropNum].m_clsid, m_metaData);
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueMetadataCollection, "MetadataCollection", system_to_clsid("MG_MDCOL"));