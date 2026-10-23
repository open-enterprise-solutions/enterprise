#include "treeConfiguration.h"

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaCommandObject.h"   // ibValueMetaObjectCommand::GetSubCommands — commands nest
#include "backend/metaCollection/metaComposerObject.h"  // the report's composers, as rows
#include "backend/metaCollection/metaGroups.h"          // what a group is called, where it stands, its band
#include "backend/metaCollection/metaLanguageObject.h"  // ibValueMetaObjectLanguage::GetLangCode — a language's row
#include "backend/metaCollection/metaObjectMetadata.h"  // ibValueMetaObjectConfiguration::GetCompileSyntax — the root's row
#include "backend/metaCollection/metaRoleObject.h"      // ibValueMetaObjectRole::GetRoleCompositionMode — a role's row
#include "backend/metaCollection/partial/chartOfAccounts.h"   // the chart's own two branches — AddChartOfAccountsItem
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/dataReport.h"  // ibValueMetaObjectReport — AddReportItem
#include "backend/backend_picture.h"                    // ibBackendPicture::SendPicture — the rows' pictures, by their ids
#include "core/fileKind.h"                              // ibFileExtension — the file a data processor or a report is kept in
#include "core/serialize/dataBuilder.h"

#include "frmserver/client/clientFrame.h"
#include "frmserver/docView/docView.h"
#include "frmserver/win/dlgs/formSelector.h"   // ibDialogSelectTypeForm — the form's kind, asked
#include "protocol/protocol.h"                  // ibProtocol::ibSchema::ibMetaData::FormType — the form's kind, its property

#include <algorithm>    // std::stable_sort — the groups in the declared order
#include <functional>   // std::function — a group fills its nested groups first
#include <vector>

#include <wx/scopeguard.h>   // wxON_BLOCK_EXIT_NULL — the form asked is shown again, however the asking ends

// The COMMON row is not a metatype's group — it is the band itself, so its label stays here, and the client draws it
// with its folder.
#define commonName _("Common")

// The labels of the groups INSIDE an object — written at each adder, because an adder is what decides that a register
// shows dimensions and resources while a catalog shows tabular sections.
#define	objectFormsName _("Forms")
#define	objectTemplatesName _("Templates")
#define objectAttributesName _("Attributes")
#define objectCommandsName _("Commands")
#define objectAccountingKindsName _("Accounting kinds")
#define objectAccountDimensionAccountingKindsName _("Account dimension accounting kinds")
#define objectDimensionsName _("Dimensions")
#define objectResourcesName _("Resources")

#define objectComposersName _("Composers")
#define objectTablesName _("Tables")
#define objectEnumerationsName _("Enums")

namespace {

// HOW A MEMBER OF THE GROUP IS PUT IN. Three shapes and no more: an ordinary row, a row that is itself a group (a
// section holds sections), and a command (which nests its sub-commands).
enum class ibMetaRow { Item, Group, Command };

// What this tree knows about a group: how a member of it is rendered, and which group it nests under. The caption,
// the band and the place are the group's own answer (backend/metaCollection/metaGroups.h).
struct ibMetaTreeGroupDef {
	ibClassID   m_clsid;
	ibClassID   m_owner;   // 0 = straight in its band; otherwise the group it nests under
	ibMetaRow   m_row;
};

const ibMetaTreeGroupDef s_groups[] = {
	{ g_metaCommonModuleCLSID,     0, ibMetaRow::Item    },
	{ g_metaCommonFormCLSID,       0, ibMetaRow::Item    },
	{ g_metaCommonCommandCLSID,    0, ibMetaRow::Command },
	{ g_metaCommandGroupCLSID,     0, ibMetaRow::Item    },
	{ g_metaCommonTemplateCLSID,   0, ibMetaRow::Item    },

	// SCHEDULED JOBS: one branch, two kinds inside it — the parameterized jobs in the branch, the predefined ones in a
	// sub-branch of it.
	{ g_metaParameterizedJobCLSID, 0, ibMetaRow::Item },
	{ g_metaScheduledJobCLSID,     g_metaParameterizedJobCLSID, ibMetaRow::Item },
	{ g_metaEventHandlerCLSID,     0, ibMetaRow::Item },

	{ g_metaSessionParameterCLSID, 0, ibMetaRow::Item },
	{ g_metaFunctionalOptionCLSID, 0, ibMetaRow::Item },
	{ g_metaCommonAttributeCLSID,  0, ibMetaRow::Item },
	{ g_metaPictureCLSID,          0, ibMetaRow::Item },
	// A section holds sections, so its rows are groups themselves.
	{ g_metaSectionCLSID,          0, ibMetaRow::Group },
	{ g_metaRoleCLSID,             0, ibMetaRow::Item },
	{ g_metaLanguageCLSID,         0, ibMetaRow::Item },

	{ g_metaConstantCLSID,                   0, ibMetaRow::Item },
	{ g_metaCatalogCLSID,                    0, ibMetaRow::Item },
	{ g_metaDocumentCLSID,                   0, ibMetaRow::Item },
	{ g_metaEnumerationCLSID,                0, ibMetaRow::Item },
	{ g_metaDataProcessorCLSID,              0, ibMetaRow::Item },
	{ g_metaReportCLSID,                     0, ibMetaRow::Item },
	{ g_metaChartOfCharacteristicTypesCLSID, 0, ibMetaRow::Item },
	{ g_metaChartOfAccountsCLSID,            0, ibMetaRow::Item },
	{ g_metaChartOfCalculationTypesCLSID,    0, ibMetaRow::Item },
	{ g_metaInformationRegisterCLSID,        0, ibMetaRow::Item },
	{ g_metaAccumulationRegisterCLSID,       0, ibMetaRow::Item },
	{ g_metaAccountingRegisterCLSID,         0, ibMetaRow::Item },
	{ g_metaCalculationRegisterCLSID,        0, ibMetaRow::Item },
	{ g_metaSequenceCLSID,                   0, ibMetaRow::Item },
};

// The rows this tree draws, in the order the declaration gives.
std::vector<const ibMetaTreeGroupDef*> OrderedGroups()
{
	std::vector<const ibMetaTreeGroupDef*> rows;
	for (const ibMetaTreeGroupDef& def : s_groups)
		rows.push_back(&def);
	std::stable_sort(rows.begin(), rows.end(),
		[](const ibMetaTreeGroupDef* a, const ibMetaTreeGroupDef* b) {
			return ibMetaGroupOrder(a->m_clsid) < ibMetaGroupOrder(b->m_clsid);
		});
	return rows;
}

// THE FILE A DATA PROCESSOR OR A REPORT IS KEPT IN apart from the configuration — the desktop's tree read one from it and
// wrote it into one (ibConfigurationTree::PrepareReplaceMenu, InsertItem). False: a kind kept in none.
bool FileKindOf(const ibClassID& clsid, ibFileKind& kind)
{
	if (clsid == g_metaDataProcessorCLSID)
		kind = ibFileKind::Tool;
	else if (clsid == g_metaReportCLSID)
		kind = ibFileKind::Report;
	else
		return false;
	return true;
}

// …an item that reads or writes it — the tree's own, the command it sends by its Name (Load, Unload), the file by its
// extension.
void AppendFileItem(ibDataNode& menu, const wxString& name, const wxString& caption, ibFileKind kind)
{
	ibDataNode& row = menu.AddChild(0, static_cast<ibMetaID>(menu.Children().size() + 1));
	row.SetValue(ibProtocol::ibSchema::ibMetaData::Kind, static_cast<std::int32_t>(ibMetaMenuKind::Editor));
	row.SetValue(ibProtocol::ibSchema::ibMetaData::Name, name);
	row.SetValue(ibProtocol::ibSchema::ibMetaData::Title, caption);
	row.SetValue(ibProtocol::ibSchema::ibMetaData::Extension, ibFileExtension(kind));
}

} // namespace

// ============================================================================
//  ibMetaTreeNotifier — the subscription, and it lives HERE
// ============================================================================
//
// Every method is one line to the tree: what the metadata keeps a list of is a small object, and nothing outside this
// module can name it.

class ibMetaTreeNotifier : public ibMetaDataNotifier {
	ibMetaTreeBase* const m_owner;
public:
	explicit ibMetaTreeNotifier(ibMetaTreeBase* owner) : m_owner(owner) {}

	virtual void MetaDataChanged() override {
		m_owner->MetaDataChanged();
	}
	virtual void MetaObjectChanged(ibMetaStage stage, ibValueMetaObject* object) override {
		m_owner->MetaObjectChanged(stage, object);
	}
};

wxString ibMetaTreeBase::ClsidText(const ibClassID& clsid)
{
	return wxString::Format(wxT("%llu"), static_cast<unsigned long long>(clsid));
}

ibMetaTreeBase::ibMetaTreeBase(ibClientFrame* frame)
	: m_frame(frame), m_notifier(new ibMetaTreeNotifier(this))
{
}

ibMetaTreeBase::~ibMetaTreeBase()
{
	WatchMetaData(nullptr);   // off whatever list it is on — only the subscription is touched here
	delete m_notifier;
}

void ibMetaTreeBase::WatchMetaData(ibMetaData* metaData)
{
	if (m_watched == metaData)
		return;
	if (m_watched != nullptr)
		m_watched->RemoveNotifier(m_notifier);
	m_watched = metaData;
	if (m_watched != nullptr)
		m_watched->AddNotifier(m_notifier);
}

// ============================================================================
//  The watch
// ============================================================================

// THE CHROME — the asterisk of a modified configuration: its state rides the root row, read with every frame, so it
// is drawn as the rows are (see MetaObjectChanged) — on its own session the call is answered with the frame; on another
// the client is told to ask for it.
void ibMetaTreeBase::MetaDataChanged()
{
	if (ibClientFrame::GetFrame() != m_frame)
		m_frame->RefreshFrame();
}

// ⭐ NO STAGE DRAWS A ROW. The rows are read from the metadata with every frame, so what a stage leaves the tree is the
// rest of what the desktop's answer did: the editors (this client's documents) and a question to the person.
//
// ⚠ AND ONLY ON THE FRAME'S OWN SESSION. A change made on another — the assistant's, over MCP — arrives on its thread,
// where this client's documents are not to be touched and there is nobody of this client's to ask: the client is only
// told to ask for its frame, which carries the rows as they now are. A change made on its own session needs no telling:
// whatever made it is a call of this client's, and a call is answered with the frame.
void ibMetaTreeBase::MetaObjectChanged(ibMetaDataNotifier::ibMetaStage stage, ibValueMetaObject* object)
{
	if (ibClientFrame::GetFrame() != m_frame) {
		m_frame->RefreshFrame();
		return;
	}

	switch (stage) {

	// GOING, ALL OF IT — the editors opened over this metadata close, once.
	case ibMetaDataNotifier::ibMetaStage::Closed:
		CloseDocuments();
		return;

	// GONE — what its row stood for closes, and everything under it.
	case ibMetaDataNotifier::ibMetaStage::Removed:
		if (object != nullptr)
			EraseItem(object);
		return;

	// THE NAME IS ALREADY THE NEW ONE — read off the object; the open editor's title follows it.
	case ibMetaDataNotifier::ibMetaStage::Renamed:
		if (object != nullptr) {
			if (ibMetaDocument* document = GetDocument(object)) {
				document->SetTitle(object->GetClassName() + wxT(": ") + object->GetName());
				document->OnChangeFilename(true);
			}
		}
		return;

	// BORN — a form asked its kind, and every open editor re-gathers: a form's command navigator lists what the
	// metadata holds.
	case ibMetaDataNotifier::ibMetaStage::Created:
		if (object != nullptr && !object->IsDeleted()) {
			if (!AskFormKind(object))
				return;   // refused and taken away again — its Removed has come and gone
			NotifyDocuments();
		}
		return;

	default:
		return;
	}
}

// ⭐ A FORM IS ASKED WHAT KIND IT IS — the desktop's wizard (ibDialogSelectTypeForm), its window the client's: the
// kinds its owner offers, and the form's Name, Synonym and Comment. Asked inside the Create that made the form — the
// client's call answers once it is answered. Only a form that has no kind yet: a paste, a copy and a tool-made form
// already know what they are.
//
// ⚠ REFUSED IS ANYTHING BUT A KIND THE OWNER OFFERED, or a generic form — the form is not wanted, and it goes again,
// through the door a Delete takes, before the Create answers. A generic form's kind is the one it was made with
// (wxNOT_FOUND): nothing is put on it.
//
// ⚠ AND NO ROW FOR IT WHILE IT IS ASKED — the question goes with a frame, whose rows are read from the metadata, where
// the form already is; the desktop's tree drew its row once the wizard was answered.
bool ibMetaTreeBase::AskFormKind(ibValueMetaObject* object)
{
	if (object == nullptr)
		return true;

	ibValueMetaObjectForm* form = dynamic_cast<ibValueMetaObjectForm*>(object);
	if (form == nullptr || form->GetTypeForm() != wxNOT_FOUND)
		return true;

	ibValueMetaObjectGenericData* owner =
		dynamic_cast<ibValueMetaObjectGenericData*>(form->GetParent());
	if (owner == nullptr)
		return true;

	ibDialogSelectTypeForm dlg(owner, form);
	const ibFormTypeList optList = owner->GetFormType();
	for (unsigned int idx = 0; idx < optList.GetItemCount(); idx++)
		dlg.AppendTypeForm(optList.GetItemName(idx), optList.GetItemLabel(idx), optList.GetItemId(idx));

	int chosen = defaultFormType;
	{
		m_askedForm = form;
		wxON_BLOCK_EXIT_NULL(m_askedForm);
		if (!dlg.ShowModal(chosen)) {
			GetMetaData()->RemoveMetaObject(form);
			return false;
		}
	}

	// Placed the way the object inspector places one: ask the owner, set, tell the owner. A generic form keeps the kind
	// it was made with.
	ibProperty* const kind = form->GetProperty(ibProtocol::ibSchema::ibMetaData::FormType);
	if (kind != nullptr && chosen != defaultFormType) {
		const wxVariant before = kind->GetValue();
		if (form->OnPropertyChanging(kind, wxVariant((long)chosen))) {
			kind->SetValue(wxVariant((long)chosen));
			form->OnPropertyChanged(kind, before, kind->GetValue());
		}
	}

	// …AND THE LAYOUT, which the owner builds once the kind is known.
	owner->OnCreateFormObject(form);
	return true;
}

// ============================================================================
//  The editors — this client's documents
// ============================================================================

void ibMetaTreeBase::NotifyDocuments() const
{
	for (auto& doc : m_frame->GetDocumentManager()->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc != nullptr) metaDoc->UpdateAllViews();
	}
}

// CLOSING THE EDITORS IS PART OF LEAVING A CONFIGURATION — the documents of this client whose metaobject is of this
// metadata. The others (the audit log, a text) have none to compare.
void ibMetaTreeBase::CloseDocuments()
{
	if (GetMetaData() == nullptr)
		return;
	for (auto& doc : m_frame->GetDocumentManager()->GetDocumentsVector()) {
		const ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc == nullptr) continue;
		const ibValueMetaObject* metaObject = metaDoc->GetMetaObject();
		if (metaObject != nullptr && metaObject->GetMetaData() == GetMetaData()) {
			doc->DeleteAllViews();
		}
	}
}

// CLOSE WHAT THE ROW STOOD FOR, AND EVERYTHING UNDER IT — an editor open on a form five levels down goes with the
// object. The desktop walked the row's subtree; here the row is the object, so an editor goes when its object is this
// one or lies under it — the object is marked, not destroyed, so its children still name it as their parent.
void ibMetaTreeBase::EraseItem(const ibValueMetaObject* object)
{
	for (auto& doc : m_frame->GetDocumentManager()->GetDocumentsVector()) {
		ibMetaDocument* metaDoc = wxDynamicCast(doc, ibMetaDocument);
		if (metaDoc == nullptr)
			continue;
		for (const ibValueMetaObject* shown = metaDoc->GetMetaObject(); shown != nullptr; shown = shown->GetParent()) {
			if (shown == object) {
				metaDoc->DeleteAllViews();
				break;
			}
		}
	}
}

ibMetaDocument* ibMetaTreeBase::GetDocument(ibValueMetaObject* metaObject) const
{
	return m_frame->GetDocumentManager()->FindOpenDocument(metaObject);
}

// ============================================================================
//  The rows
// ============================================================================

ibDataNode& ibMetaTreeBase::AppendGroupItem(ibDataNode& parent, const ibClassID& clsid, const wxString& name)
{
	// A group row is named by its place: it carries no object, and its place under its parent is fixed by the layout.
	const ibMetaID place = -static_cast<ibMetaID>(parent.Children().size() + 1);
	ibDataNode& item = parent.AddChild(0, place);
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Title, name);
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Picture, ibBackendPicture::SendPicture(clsid));
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Group, ClsidText(clsid));
	// …and a group of what is kept in a file apart: one read in from it (the desktop's InsertItem).
	ibFileKind kind = ibFileKind::Tool;
	if (FileKindOf(clsid, kind))
		AppendFileItem(item.Child(ibProtocol::ibSchema::ibMetaData::Menu), wxT("Load"), _("Insert data processor, report..."), kind);
	return item;
}

ibDataNode& ibMetaTreeBase::AppendGroupItem(ibDataNode& parent, const ibClassID& clsid, ibValueMetaObject* metaObject)
{
	ibDataNode& item = AppendItem(parent, metaObject);
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Group, ClsidText(clsid));
	return item;
}

ibDataNode& ibMetaTreeBase::AppendItem(ibDataNode& parent, ibValueMetaObject* metaObject)
{
	ibDataNode& item = parent.AddChild(0, metaObject->GetMetaID());
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Title, metaObject->GetName());
	// Every metaobject's icon is its class's.
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Picture, ibBackendPicture::SendPicture(metaObject->GetClassType()));
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Type, ClsidText(metaObject->GetClassType()));
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Synonym, metaObject->GetSynonym());
	// …and its guid — what a record of the base keeps it by (a user's roles and language: ibDialogUserItem).
	item.SetValue(ibProtocol::ibSchema::ibMetaData::Guid, metaObject->GetDocPath());
	if (const ibValueMetaObject* const owner = metaObject->GetParent())
		item.SetValue(ibProtocol::ibSchema::ibMetaData::Position, static_cast<std::int32_t>(owner->GetChildPosition(metaObject)));
	AppendMenu(item, metaObject);
	return item;
}

void ibMetaTreeBase::AppendMenu(ibDataNode& item, ibValueMetaObject* metaObject)
{
	std::vector<ibMetaMenuItem> items;
	const bool standardSuppressed = metaObject->CollectContextMenu(items);
	ibFileKind kind = ibFileKind::Tool;
	const bool keptInFile = FileKindOf(metaObject->GetClassType(), kind);
	if (items.empty() && !standardSuppressed && !keptInFile)
		return;

	ibDataNode& menu = item.Child(ibProtocol::ibSchema::ibMetaData::Menu);
	if (standardSuppressed)
		menu.SetValue(ibProtocol::ibSchema::ibMetaData::Standard, false);

	for (const ibMetaMenuItem& entry : items) {
		ibDataNode& row = menu.AddChild(0, static_cast<ibMetaID>(menu.Children().size() + 1));
		row.SetValue(ibProtocol::ibSchema::ibMetaData::Kind, static_cast<std::int32_t>(entry.m_kind));
		row.SetValue(ibProtocol::ibSchema::ibMetaData::Name, entry.m_name);
		row.SetValue(ibProtocol::ibSchema::ibMetaData::Title, entry.m_caption);
		// What it opens, by its id — and of what class, so a module no row shows (an object's inner module) is made
		// on the far side, under the object whose row this is.
		if (entry.m_metaObject != nullptr) {
			row.SetValue(ibProtocol::ibSchema::ibMetaData::Object, static_cast<std::int32_t>(entry.m_metaObject->GetMetaID()));
			row.SetValue(ibProtocol::ibSchema::ibMetaData::Type, ClsidText(entry.m_metaObject->GetClassType()));
		}
		// Its picture — of what it opens, else its own (the desktop's AppendMetaMenu).
		if (entry.m_metaObject != nullptr)
			row.SetValue(ibProtocol::ibSchema::ibMetaData::Picture, ibBackendPicture::SendPicture(entry.m_metaObject->GetClassType()));
		else if (entry.m_picture != 0)
			row.SetValue(ibProtocol::ibSchema::ibMetaData::Picture, ibBackendPicture::SendPicture(entry.m_picture));
		if (entry.m_id != wxNOT_FOUND)
			row.SetValue(ibProtocol::ibSchema::ibMetaData::Command, static_cast<std::int32_t>(entry.m_id));
	}

	// …and the file it is kept in apart, as the desktop's tree offered it (PrepareReplaceMenu): read over it, written out.
	if (keptInFile) {
		AppendFileItem(menu, wxT("Load"), _("Replace data processor, report..."), kind);
		AppendFileItem(menu, wxT("Unload"), _("Save data processor, report..."), kind);
	}
}

// A command node and, recursively, its sub-commands: a group command holds commands the way a subsystem holds
// subsystems.
void ibMetaTreeBase::AppendCommandNode(ibDataNode& parent, ibValueMetaObject* command)
{
	if (command == nullptr || command->IsDeleted())
		return;
	ibDataNode& hCmd = AppendItem(parent, command);
	if (command->GetClassType() == g_metaCommonCommandCLSID || command->GetClassType() == g_metaCommandCLSID)
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(command)->GetSubCommands())
			AppendCommandNode(hCmd, sub);
}

// ============================================================================
//  The configuration's tree
// ============================================================================

ibConfigurationTree::ibConfigurationTree(ibClientFrame* frame)
	: ibMetaTreeBase(frame)
{
}

bool ibConfigurationTree::Load(std::shared_ptr<ibMetaDataConfigurationBase> metaData)
{
	CloseDocuments();   // a configuration is being left — its editors go with it
	m_metaData = std::move(metaData);
	WatchMetaData(GetMetaData());   // off the old list, onto this one — one call, one place
	return m_metaData != nullptr;
}

void ibConfigurationTree::OnDraw(ibDataNode& root)
{
	if (m_metaData != nullptr)
		FillData(root);
}

// FILL ONE GROUP OF AN OBJECT — what its owner hosts of it.
template <typename TArray>
void ibConfigurationTree::AppendObjectGroup(ibDataNode& parent, const ibClassID& groupClsid,
	const wxString& label, const TArray& objects)
{
	ibDataNode& group = AppendGroupItem(parent, groupClsid, label);
	for (auto object : objects) {
		if (!object->IsAcceptedByParent())   // gone, or a kind its owner does not host — one question
			continue;
		if (object == m_askedForm)            // not made yet: the person is asked what kind it is
			continue;
		AppendItem(group, object);
	}
}

// TABLES — the one group whose rows are groups themselves: each table shows its own columns.
template <typename TArray>
void ibConfigurationTree::AppendTableGroup(ibDataNode& parent, const ibClassID& tableClsid,
	const wxString& label, const TArray& tables)
{
	ibDataNode& group = AppendGroupItem(parent, tableClsid, label);
	for (auto metaTable : tables) {
		// A PREDEFINED section is not shown: its owner does not accept that clsid as a child.
		if (!metaTable->IsAcceptedByParent())
			continue;
		ibClassID columnClsid = 0;
		std::vector<ibValueMetaObject*> columns;
		TableColumns(metaTable, columnClsid, columns);
		ibDataNode& hTable = AppendGroupItem(group, columnClsid, metaTable);
		for (auto column : columns) {
			if (!column->IsAcceptedByParent())
				continue;
			AppendItem(hTable, column);
		}
	}
}

// COMMANDS — the rows nest.
template <typename TArray>
void ibConfigurationTree::AppendCommandGroup(ibDataNode& parent, const wxString& label, const TArray& commands)
{
	ibDataNode& group = AppendGroupItem(parent, g_metaCommandCLSID, label);
	for (auto metaCommand : commands)
		AppendCommandNode(group, metaCommand);
}

void ibConfigurationTree::AddInterfaceItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	ibValueMetaObjectSection* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectSection>();
	wxASSERT(metaObjectValue);

	// SECTIONS NEST.
	for (auto commonInterface : metaObjectValue->GetInterfaceArrayObject()) {
		if (commonInterface->IsDeleted())
			continue;
		ibDataNode& hSection = AppendGroupItem(hParentID, g_metaSectionCLSID, commonInterface);
		AddInterfaceItem(commonInterface, hSection);
	}

	for (auto metaCommand : metaObjectValue->GetCommandArrayObject())   // a section owns its own commands
		AppendCommandNode(hParentID, metaCommand);
}

// A REFERENCE OBJECT — attributes, tabular sections, forms, commands, templates. Catalogs and documents render
// identically, and so do the two charts and a parameterized job (ExpandMetaItem).
void ibConfigurationTree::AddCatalogItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	ibValueMetaObjectRecordDataRef* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordDataRef>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	// A catalog / document is ALWAYS a reference, so its table is the DB-backed MD_TBLR.
	AppendTableGroup(hParentID, g_metaTableRefCLSID, objectTablesName,
		metaObjectValue->GetTableArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A CHART OF ACCOUNTS — a reference object with two branches of its own, standing between the attributes and the
// tabular sections.
void ibConfigurationTree::AddChartOfAccountsItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	ibValueMetaObjectChartOfAccounts* chart = metaObject->ConvertToType<ibValueMetaObjectChartOfAccounts>();
	wxASSERT(chart);

	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		chart->GetAttributeArrayObject());
	AppendObjectGroup(hParentID, g_metaAccountingKindCLSID, objectAccountingKindsName,
		chart->GetAccountingKindArrayObject());
	AppendObjectGroup(hParentID, g_metaAccountDimensionAccountingKindCLSID, objectAccountDimensionAccountingKindsName,
		chart->GetAccountDimensionAccountingKindArrayObject());
	AppendTableGroup(hParentID, g_metaTableRefCLSID, objectTablesName,
		chart->GetTableArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		chart->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, chart->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		chart->GetTemplateArrayObject());
}

// A DOCUMENT renders exactly as a catalog does.
void ibConfigurationTree::AddDocumentItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	AddCatalogItem(metaObject, hParentID);
}

// AN ENUMERATION has values where the others have attributes and tabular sections.
void ibConfigurationTree::AddEnumerationItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	ibValueMetaObjectRecordDataEnumRef* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordDataEnumRef>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaEnumCLSID, objectEnumerationsName,
		metaObjectValue->GetEnumObjectArray());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A DATA PROCESSOR — the same five groups as a catalog, its tabular sections in RAM (MD_TBL).
void ibConfigurationTree::AddDataProcessorItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	ibValueMetaObjectRecordData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRecordData>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	AppendTableGroup(hParentID, g_metaTableCLSID, objectTablesName,
		metaObjectValue->GetTableArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A REPORT is a data processor plus its COMPOSERS.
void ibConfigurationTree::AddReportItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	AddDataProcessorItem(metaObject, hParentID);   // same shape, down to the RAM tabular sections

	ibValueMetaObjectReport* metaReport = metaObject->ConvertToType<ibValueMetaObjectReport>();
	wxASSERT(metaReport);

	AppendObjectGroup(hParentID, g_metaComposerCLSID, objectComposersName,
		metaReport->GetComposerArrayObject());
}

// A REGISTER — dimensions and resources where a reference object has tabular sections.
void ibConfigurationTree::AddInformationRegisterItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	ibValueMetaObjectRegisterData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRegisterData>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaDimensionCLSID, objectDimensionsName,
		metaObjectValue->GetDimensionArrayObject());
	AppendObjectGroup(hParentID, g_metaResourceCLSID, objectResourcesName,
		metaObjectValue->GetResourceArrayObject());
	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

void ibConfigurationTree::AddAccumulationRegisterItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	AddInformationRegisterItem(metaObject, hParentID);   // same shape — an accounting register too
}

// A SEQUENCE UNFOLDS AS A REGISTER DOES, less the resources: it measures nothing.
void ibConfigurationTree::AddSequenceItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	ibValueMetaObjectRegisterData* metaObjectValue = metaObject->ConvertToType<ibValueMetaObjectRegisterData>();
	wxASSERT(metaObjectValue);

	AppendObjectGroup(hParentID, g_metaDimensionCLSID, objectDimensionsName,
		metaObjectValue->GetDimensionArrayObject());
	AppendObjectGroup(hParentID, g_metaAttributeCLSID, objectAttributesName,
		metaObjectValue->GetAttributeArrayObject());
	AppendObjectGroup(hParentID, g_metaFormCLSID, objectFormsName,
		metaObjectValue->GetFormArrayObject());
	AppendCommandGroup(hParentID, objectCommandsName, metaObjectValue->GetCommandArrayObject());
	AppendObjectGroup(hParentID, g_metaTemplateCLSID, objectTemplatesName,
		metaObjectValue->GetTemplateArrayObject());
}

// A CALCULATION REGISTER — the same shape.
void ibConfigurationTree::AddCalculationRegisterItem(ibValueMetaObject* metaObject, ibDataNode& hParentID)
{
	AddInformationRegisterItem(metaObject, hParentID);
}

// HOW A METAOBJECT UNFOLDS — the ONE dispatcher. A chart of characteristic types and a chart of calculation types
// render AS a catalog, an accounting register AS an accumulation register, a parameterized job AS a catalog entry.
void ibConfigurationTree::ExpandMetaItem(ibValueMetaObject* metaItem, ibDataNode& item)
{
	const ibClassID clsid = metaItem->GetClassType();

	if      (clsid == g_metaCatalogCLSID)                    AddCatalogItem(metaItem, item);
	else if (clsid == g_metaDocumentCLSID)                   AddDocumentItem(metaItem, item);
	else if (clsid == g_metaEnumerationCLSID)                AddEnumerationItem(metaItem, item);
	else if (clsid == g_metaDataProcessorCLSID)              AddDataProcessorItem(metaItem, item);
	else if (clsid == g_metaReportCLSID)                     AddReportItem(metaItem, item);
	else if (clsid == g_metaInformationRegisterCLSID)        AddInformationRegisterItem(metaItem, item);
	else if (clsid == g_metaAccumulationRegisterCLSID)       AddAccumulationRegisterItem(metaItem, item);
	else if (clsid == g_metaParameterizedJobCLSID)           AddCatalogItem(metaItem, item);
	else if (clsid == g_metaChartOfCharacteristicTypesCLSID) AddCatalogItem(metaItem, item);
	else if (clsid == g_metaChartOfCalculationTypesCLSID)    AddCatalogItem(metaItem, item);
	else if (clsid == g_metaChartOfAccountsCLSID)            AddChartOfAccountsItem(metaItem, item);
	else if (clsid == g_metaAccountingRegisterCLSID)         AddAccumulationRegisterItem(metaItem, item);
	else if (clsid == g_metaCalculationRegisterCLSID)        AddCalculationRegisterItem(metaItem, item);
	else if (clsid == g_metaSequenceCLSID)                   AddSequenceItem(metaItem, item);
	else if (clsid == g_metaSectionCLSID)                    AddInterfaceItem(metaItem, item);

	// A LANGUAGE SAYS ITS CODE — what a translated text is keyed by: the translation window's boxes are the languages',
	// each by its code (ibDialogTranslateConstructor).
	else if (clsid == g_metaLanguageCLSID)
		item.SetValue(ibProtocol::ibSchema::ibMetaData::Code, static_cast<ibValueMetaObjectLanguage*>(metaItem)->GetLangCode());

	// A ROLE SAYS HOW IT COMBINES — what a user's membership of it is written with (ibDialogUserItem).
	else if (clsid == g_metaRoleCLSID)
		item.SetValue(ibProtocol::ibSchema::ibMetaData::Mode, static_cast<std::int32_t>(static_cast<ibValueMetaObjectRole*>(metaItem)->GetRoleCompositionMode()));

	// A COMMAND HOLDS COMMANDS.
	else if (clsid == g_metaCommandCLSID || clsid == g_metaCommonCommandCLSID) {
		for (auto sub : static_cast<ibValueMetaObjectCommand*>(metaItem)->GetSubCommands())
			AppendCommandNode(item, sub);
	}

	// A TABLE shows its own columns.
	else {
		ibClassID columnClsid = 0;
		std::vector<ibValueMetaObject*> columns;
		if (TableColumns(metaItem, columnClsid, columns)) {
			for (auto column : columns) {
				if (!column->IsAcceptedByParent())
					continue;
				AppendItem(item, column);
			}
		}
	}
	// Anything else is a leaf row — a module, a form, a picture, a role, a language.
}

bool ibConfigurationTree::TableColumns(ibValueMetaObject* table, ibClassID& columnClsid, std::vector<ibValueMetaObject*>& columns) const
{
	const ibClassID clsid = table->GetClassType();
	if (clsid == g_metaTableCLSID || clsid == g_metaTableRefCLSID) {
		ibValueMetaObjectTableData* metaTable = table->ConvertToType<ibValueMetaObjectTableData>();
		wxASSERT(metaTable);
		columnClsid = g_metaAttributeCLSID;
		if (metaTable != nullptr)
			for (auto attribute : metaTable->GetAttributeArrayObject())
				columns.push_back(attribute);
		return true;
	}
	return false;
}

// ONE PASS OVER THE LAYOUT. The root is the configuration's row; under it Common and the metadata's groups, each group
// filled with what the metadata holds of its kind.
//
// ⚠ A ROW IS WRITTEN WHOLE BEFORE ITS NEXT SIBLING — a node holds its children by value, so a reference to one does
// not survive the next one added beside it. So the desktop's InitTree (every group row first) and its fill (each group's
// objects after) are one walk: Common with its groups, then the root's own; a nested group (the predefined jobs) inside
// its owner's, before the owner's objects, where InitTree put it.
void ibConfigurationTree::FillData(ibDataNode& root)
{
	ibValueMetaObject* commonMetadata = m_metaData->GetCommonMetaObject();
	wxASSERT(commonMetadata);

	root.SetMetaId(commonMetadata->GetMetaID());
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Title, m_metaData->GetConfigName());
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Picture, ibBackendPicture::SendPicture(g_metaCommonMetadataCLSID));
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Type, ClsidText(commonMetadata->GetClassType()));
	// …the dialect its modules are written in — what the desktop's process held (ibCompileCode::GetCodeStyle).
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Syntax, static_cast<std::int32_t>(m_metaData->GetCommonMetaObject()->GetCompileSyntax()));
	// …and its state — what the desktop's caption asked of it (ibFrontendMainFrameDesigner::Modify).
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Modified, m_metaData->IsModified());
	root.SetValue(ibProtocol::ibSchema::ibMetaData::Edited, m_metaData->IsEdited());
	root.SetValue(ibProtocol::ibSchema::ibMetaData::ConfigSave, m_metaData->IsConfigSave());
	// …and the kinds Open opens an editor of (ibDocManager::OpenForm): a catalog's or a document's row opens nothing,
	// and the client does not ask for it.
	ibDataNode& editors = root.Child(ibProtocol::ibSchema::ibMetaData::Editors);
	for (const ibClassID& clsid : m_frame->GetDocumentManager()->GetMetaTemplateClasses()) {
		if (clsid == 0)
			continue;   // a template of no metaobject's (a text, a spreadsheet)
		ibDataNode& editor = editors.AddChild(0, static_cast<ibMetaID>(editors.Children().size() + 1));
		editor.SetValue(ibProtocol::ibSchema::ibMetaData::Type, ClsidText(clsid));
	}
	AppendMenu(root, commonMetadata);

	const std::vector<const ibMetaTreeGroupDef*> rows = OrderedGroups();

	// One group, its nested groups and its objects.
	const std::function<void(ibDataNode&, const ibMetaTreeGroupDef*)> fillGroup =
		[this, &rows, &fillGroup](ibDataNode& parent, const ibMetaTreeGroupDef* def) {

		ibDataNode& group = AppendGroupItem(parent, def->m_clsid, ibMetaGroupCaption(def->m_clsid));
		for (const ibMetaTreeGroupDef* nested : rows) {
			if (nested->m_owner == def->m_clsid)
				fillGroup(group, nested);
		}

		for (auto metaObject : m_metaData->GetAnyArrayObject(def->m_clsid)) {

			if (metaObject->IsDeleted())
				continue;

			switch (def->m_row) {
			case ibMetaRow::Command:
				AppendCommandNode(group, metaObject);   // hub — nests sub-commands, skips deleted
				break;
			case ibMetaRow::Group:
				// A section holds sections, so its row is a group row in its own right.
				ExpandMetaItem(metaObject, AppendGroupItem(group, def->m_clsid, metaObject));
				break;
			default:
				ExpandMetaItem(metaObject, AppendItem(group, metaObject));
				break;
			}
		}
	};

	ibDataNode& common = root.AddChild(0, -1);
	common.SetValue(ibProtocol::ibSchema::ibMetaData::Title, commonName);
	for (const ibMetaTreeGroupDef* def : rows) {
		if (def->m_owner == 0 && ibMetaGroupBandOf(def->m_clsid) == ibMetaGroupBand::Common)
			fillGroup(common, def);
	}
	for (const ibMetaTreeGroupDef* def : rows) {
		if (def->m_owner == 0 && ibMetaGroupBandOf(def->m_clsid) != ibMetaGroupBand::Common)
			fillGroup(root, def);
	}
}
