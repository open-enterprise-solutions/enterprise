#include "treeDataProcessor.h"

#include "backend/backend_picture.h"   // ibBackendPicture::SendPicture — the root's picture, by its id
#include "core/serialize/dataBuilder.h"

#include "frmserver/client/clientFrame.h"
#include "frmserver/client/propertyInspector.h"   // the header's rows
#include "frmserver/docView/docView.h"            // the file's document — its title

ibDataProcessorTree::ibDataProcessorTree(ibClientFrame* frame, ibMetaDocument* docParent)
	: ibMetaTreeBase(frame), m_docParent(docParent)
{
}

// The title alone: its file is the session's temporary one, kept by its id. On the frame's own session only — the
// base's rule: another's thread touches none of this client's documents.
void ibDataProcessorTree::MetaObjectChanged(ibMetaDataNotifier::ibMetaStage stage, ibValueMetaObject* object)
{
	if (ibClientFrame::GetFrame() == m_frame && m_docParent != nullptr && m_metaData != nullptr
		&& object != nullptr && object == m_metaData->GetCommonMetaObject() && m_docParent->GetTitle() != object->GetName()) {
		m_docParent->SetTitle(object->GetName());
		m_docParent->OnChangeFilename(true);
	}
	ibMetaTreeBase::MetaObjectChanged(stage, object);
}

bool ibDataProcessorTree::Load(std::shared_ptr<ibMetaDataDataProcessor> metaData)
{
	CloseDocuments();   // a file is being left — its editors go with it
	m_metaData = std::move(metaData);
	WatchMetaData(GetMetaData());   // off the old list, onto this one — one call, one place
	return m_metaData != nullptr;
}

void ibDataProcessorTree::OnDraw(ibDataNode& root)
{
	if (m_metaData != nullptr)
		FillData(root);
}

// THE DESKTOP'S GROUPS, IN ITS ORDER — the same object edited inside a configuration shows the same five
// (ibConfigurationTree::AddDataProcessorItem).
void ibDataProcessorTree::FillData(ibDataNode& root)
{
	ibValueMetaObjectDataProcessor* commonMetadata = m_metaData->GetDataProcessor();
	wxASSERT(commonMetadata);

	root.SetMetaId(commonMetadata->GetMetaID());
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Title, commonMetadata->GetName());
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Picture, ibBackendPicture::SendPicture(g_metaDataProcessorCLSID));
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Type, ClsidText(commonMetadata->GetClassType()));
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Synonym, commonMetadata->GetSynonym());
	// …and its header, the fields above the rows — its properties' rows, as the inspector writes them.
	ibDataNode& header = root.Child(ibProtocol::ibSchema::ibMetaData::Properties);
	for (const ibChar* name : { ibT("Name"), ibT("Synonym"), ibT("Comment"), ibT("DefaultFormObject") }) {
		if (ibProperty* const property = commonMetadata->GetProperty(name))
			ibPropertyInspector::AppendProperty(header, property);
	}
	AppendMenu(root, commonMetadata);

	// attribute list
	ibDataNode& attributes = AppendGroupItem(root, g_metaAttributeCLSID, _("Attributes"));
	for (auto attribute : commonMetadata->GetAttributeArrayObject()) {
		if (!attribute->IsAcceptedByParent())
			continue;
		AppendItem(attributes, attribute);
	}

	// tabular section list — RAM tabular sections: a processor is not a reference
	ibDataNode& tables = AppendGroupItem(root, g_metaTableCLSID, _("Tables"));
	for (auto metaTable : commonMetadata->GetTableArrayObject()) {
		if (!metaTable->IsAcceptedByParent())   // predefined section — same rule as the attributes above
			continue;
		ibDataNode& hItem = AppendGroupItem(tables, g_metaAttributeCLSID, metaTable);
		for (auto attribute : metaTable->GetAttributeArrayObject()) {
			if (!attribute->IsAcceptedByParent())
				continue;
			AppendItem(hItem, attribute);
		}
	}

	// forms
	ibDataNode& forms = AppendGroupItem(root, g_metaFormCLSID, _("Forms"));
	for (auto metaForm : commonMetadata->GetFormArrayObject()) {
		if (metaForm->IsDeleted() || metaForm == m_askedForm)   // gone, or not made yet: its kind is being asked
			continue;
		AppendItem(forms, metaForm);
	}

	// commands — nests its sub-commands, skips deleted
	ibDataNode& commands = AppendGroupItem(root, g_metaCommandCLSID, _("Commands"));
	for (auto metaCommand : commonMetadata->GetCommandArrayObject())
		AppendCommandNode(commands, metaCommand);

	// templates
	ibDataNode& templates = AppendGroupItem(root, g_metaTemplateCLSID, _("Templates"));
	for (auto metaTemplates : commonMetadata->GetTemplateArrayObject()) {
		if (metaTemplates->IsDeleted())
			continue;
		AppendItem(templates, metaTemplates);
	}
}
