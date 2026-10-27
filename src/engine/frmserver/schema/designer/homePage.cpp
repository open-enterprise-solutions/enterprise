#include "homePage.h"

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaFormObject.h"         // ibValueMetaObjectFormBase — what the start page shows
#include "backend/metaCollection/metaObjectMetadata.h"     // ibValueMetaObjectConfiguration — whose workspace it is
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include "backend/backend_picture.h"              // ibBackendPicture::SendPicture — the forms' pictures, by their ids
#include "frmserver/schema/designer/metaData.h"   // ibSchemaMetaData — whose tree, which object

namespace {

// The root Id names — refused, null, when there is no such, or it is no configuration's root.
ibValueMetaObjectConfiguration* FindRoot(const ibSession& session, const ibDataNode& args, ibProtocol::ibRefusal& refusal,
	ibString& error)
{
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibHomePage::Id, refusal, error) : nullptr;
	if (object == nullptr)
		return nullptr;

	ibValueMetaObjectConfiguration* const root = dynamic_cast<ibValueMetaObjectConfiguration*>(object);
	if (root == nullptr) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibString::Format(ibT("'%s' has no home page"), object->GetName());
	}
	return root;
}

} // namespace

bool ibSchemaHomePage::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaHomePage::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	const ibValueMetaObjectConfiguration* const root = FindRoot(session, args, refusal, error);
	if (root == nullptr)
		return;

	ibDataValue homePage;
	if (root->GetHomePage().WriteNode(homePage))
		result.SetProperty(ibProtocol::ibSchema::ibHomePage::HomePage, homePage);
	if (!root->IsEditable())
		result.SetValue(ibProtocol::ibSchema::ibHomePage::ReadOnly, true);

	// The forms the desktop's picker listed (ibDialogHomePageFormSelect::FillTree), and the workspace's rows named by:
	// every form the configuration allows, a common one among the common, any other under its owner.
	const ibMetaData* const metaData = root->GetMetaData();
	ibDataNode& forms = result.Child(ibProtocol::ibSchema::ibHomePage::Forms);
	for (const ibValueMetaObjectFormBase* metaForm :
		metaData->GetAnyArrayObject<ibValueMetaObjectFormBase>({ g_metaCommonFormCLSID, g_metaFormCLSID }, true)) {

		if (metaForm == nullptr || !metaForm->IsAllowed())
			continue;

		ibDataNode& row = forms.AddChild(0, metaForm->GetMetaID());
		row.SetValue(ibProtocol::ibSchema::ibHomePage::Id, static_cast<std::int32_t>(metaForm->GetMetaID()));
		row.SetValue(ibProtocol::ibSchema::ibHomePage::Name, metaForm->GetName());
		row.SetValue(ibProtocol::ibSchema::ibHomePage::FullName, metaForm->GetFullName());
		row.SetValue(ibProtocol::ibSchema::ibHomePage::Picture, ibBackendPicture::SendPicture(metaForm->GetClassType()));
		if (metaForm->GetClassType() == g_metaCommonFormCLSID) {
			row.SetValue(ibProtocol::ibSchema::ibHomePage::Common, true);
			continue;
		}
		if (const ibValueMetaObject* const owner = metaForm->GetParent()) {
			row.SetValue(ibProtocol::ibSchema::ibHomePage::Owner, static_cast<std::int32_t>(owner->GetMetaID()));
			row.SetValue(ibProtocol::ibSchema::ibHomePage::OwnerName, owner->GetFullName());
			row.SetValue(ibProtocol::ibSchema::ibHomePage::OwnerPicture, ibBackendPicture::SendPicture(owner->GetClassType()));
		}
	}
}

std::function<void()> ibSchemaHomePage::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	if (static_cast<ibProtocol::ibSchema::ibCommand>(command) != ibProtocol::ibSchema::ibCommand::Save) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("the home page takes Save alone");
		return nullptr;
	}

	ibValueMetaObjectConfiguration* const root = FindRoot(session, args, refusal, error);
	if (root == nullptr)
		return nullptr;

	// …as the root reads it from its file (ReadData): what travelled as a node of its own.
	const auto node = args.FindChild(ibProtocol::ibSchema::ibHomePage::HomePage);
	ibHomePageDescription homePage;
	if (!node || !homePage.ReadNode(ibDataValue::Child(std::make_shared<ibDataNode>(*node)))) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("which home page: HomePage");
		return nullptr;
	}
	if (!root->IsEditable()) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibString::Format(ibT("'%s' takes no edit"), root->GetName());
		return nullptr;
	}

	// …and the configuration modified by it — the desktop's dialog marked it on OK.
	return [root, homePage]() {
		if (root->GetHomePage() == homePage)
			return;
		root->SetHomePage(homePage);
		if (ibMetaData* const metaData = root->GetMetaData())
			metaData->Modify(true);
	};
}
