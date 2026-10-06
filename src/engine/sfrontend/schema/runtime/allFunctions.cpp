#include "allFunctions.h"

#include "backend/appData.h"                               // the base's configuration
#include "backend/backend_picture.h"                       // the icons, as they travel
#include "backend/functionalOption/functionalOptionGate.h"  // ibFunctionalOptionGate::IsAvailable
#include "backend/metaCollection/genericData.h"
#include "backend/metadataConfiguration.h"
#include "backend/serialize/dataBuilder.h"

#include <vector>

namespace {

// One group of the tree — the objects of one kind.
struct ibFunctionGroup {
	ibClassID clsid;
	wxString  title;
};

// The dialog's groups, in its order: the functional options FIRST — they decide which parts of the system are there
// at all — then what a person works with, the registers after what they are expressed in, the scheduled jobs LAST
// (they are administration, not the work this is opened for; the predefined half has no list to open).
std::vector<ibFunctionGroup> FunctionGroups()
{
	return {
		{ g_metaFunctionalOptionCLSID,           _("Functional options") },
		{ g_metaConstantCLSID,                   _("Constants") },
		{ g_metaCatalogCLSID,                    _("Catalogs") },
		{ g_metaDocumentCLSID,                   _("Documents") },
		{ g_metaDataProcessorCLSID,              _("Data processors") },
		{ g_metaReportCLSID,                     _("Reports") },
		{ g_metaChartOfCharacteristicTypesCLSID, _("Charts of characteristic types") },
		{ g_metaChartOfAccountsCLSID,            _("Charts of accounts") },
		{ g_metaChartOfCalculationTypesCLSID,    _("Charts of calculation types") },
		{ g_metaInformationRegisterCLSID,        _("Information registers") },
		{ g_metaAccumulationRegisterCLSID,       _("Accumulation registers") },
		{ g_metaAccountingRegisterCLSID,         _("Accounting registers") },
		{ g_metaCalculationRegisterCLSID,        _("Calculation registers") },
		{ g_metaSequenceCLSID,                   _("Sequences") },
		{ g_metaParameterizedJobCLSID,           _("Scheduled jobs") },
	};
}

// What a group offers this person — one filter for the tree and for what may be opened from it: available (no
// functional option has switched it off) and shown to them (AccessRight_Show).
std::vector<const ibValueMetaObjectGenericData*> OfferedObjects(const ibMetaDataConfigurationBase* metaData,
	const ibClassID& clsid)
{
	std::vector<const ibValueMetaObjectGenericData*> offered;
	for (const ibValueMetaObjectGenericData* object : metaData->GetAnyArrayObject<ibValueMetaObjectGenericData>(clsid)) {
		if (ibFunctionalOptionGate::IsAvailable(object) && object->AccessRight_Show())
			offered.push_back(object);
	}
	return offered;
}

} // namespace

bool ibSchemaAllFunctions::AccessRight(const ibApplicationInstance* applicationInstance) const
{
	const ibMetaDataConfigurationBase* const metaData = ibApplicationInstance::GetActiveMetaData(applicationInstance);
	return metaData != nullptr && metaData->AccessRight_ModeAllFunction();
}

void ibSchemaAllFunctions::Build(const ibApplicationInstance* applicationInstance, ibDataNode& tree) const
{
	const ibMetaDataConfigurationBase* const metaData = ibApplicationInstance::GetActiveMetaData(applicationInstance);
	if (metaData == nullptr)
		return;

	const std::vector<ibFunctionGroup> groups = FunctionGroups();
	ibDataNode& nodes = tree.Child(wxT("Groups"));
	for (std::size_t i = 0; i < groups.size(); ++i) {
		ibDataNode& group = nodes.AddChild(0, static_cast<ibMetaID>(i));
		group.SetValue(wxT("Title"), groups[i].title);
		const ibServerPicture groupIcon = ibBackendPicture::GetServerPicture(groups[i].clsid);
		if (groupIcon.IsOk())
			group.SetValue(wxT("Icon"), wxString(groupIcon.GetData()));

		for (const ibValueMetaObjectGenericData* object : OfferedObjects(metaData, groups[i].clsid)) {
			ibDataNode& item = group.AddChild(0, object->GetMetaID());
			item.SetValue(wxT("Item"), static_cast<s32>(object->GetMetaID()));
			item.SetValue(wxT("Title"), object->GetSynonym());
			const ibServerPicture icon = ibBackendPicture::GetServerPicture(object->GetClassType());
			if (icon.IsOk())
				item.SetValue(wxT("Icon"), wxString(icon.GetData()));
		}
	}
}

std::function<void()> ibSchemaAllFunctions::Command(const ibApplicationInstance* applicationInstance, s32 command,
	const ibDataNode& args, ibClientRefusal& refusal, wxString& error) const
{
	if (static_cast<ibSchemaAllFunctionsCommand>(command) != ibSchemaAllFunctionsCommand::Open) {
		refusal = ibClientRefusal::NotFound;
		error = wxString::Format(wxT("All functions has no command %d"), command);
		return nullptr;
	}

	const ibMetaDataConfigurationBase* const metaData = ibApplicationInstance::GetActiveMetaData(applicationInstance);
	if (metaData == nullptr) {
		refusal = ibClientRefusal::Failed;
		error = wxT("the base has no configuration");
		return nullptr;
	}

	const ibMetaID itemId = args.GetValue<s32>(wxT("Item"));
	s32 type = ibInterfaceCommandType::ibInterfaceCommandType_Default;
	args.GetValue(wxT("Type"), type);
	const ibInterfaceCommandType commandType = static_cast<ibInterfaceCommandType>(type);

	// Only what the tree offers this person: the same groups, the same gate, the same right.
	for (const ibFunctionGroup& group : FunctionGroups()) {
		for (const ibValueMetaObjectGenericData* object : OfferedObjects(metaData, group.clsid)) {
			// As the dialog's double click: the object executes what its kind answers to.
			if (object->GetMetaID() == itemId)
				return [object, commandType]() { object->Execute(commandType); };
		}
	}

	refusal = ibClientRefusal::NotFound;
	error = wxString::Format(wxT("All functions offers no item %d"), itemId);
	return nullptr;
}
