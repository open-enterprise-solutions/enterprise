////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree window
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"

#include <algorithm>

#include <wx/clipbrd.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/wupdlock.h>

#include "clipboardLock.h"   // the Open/Close pair, taken as a guard — the desktop's
#include "core/fileKind.h"   // ibFileKindOf, ibFileFilter — the file a data processor or a report is kept in
#include "designer-thin/docManager/templates/docViewModuleEditor.h"   // a module's tab — the line a stop shows
#include "frmclient/artProvider/artProvider.h"
#include "frmclient/backend/metaCollection/metaObjectMetadata.h"      // the objects a modal editor of its menu is over
#include "frmclient/backend/metaCollection/partial/commonObject.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/objinspect/objinspect.h"   // the inspector the row's object is shown in
#include "frmclient/win/picture.h"

namespace {

// A NEW BLOCK STARTS HERE — the desktop's (designer/mainFrame/metaTree/treeConfiguration_impl.cpp): asked of the MENU,
// which is the only thing that knows what is already in it — never a line first, never two in a row.
void ibAppendSeparator(wxMenu* menu)
{
	if (menu == nullptr)
		return;

	const size_t count = menu->GetMenuItemCount();
	if (count == 0)
		return;   // nothing above it — the line would be the first thing in the menu

	const wxMenuItem* last = menu->FindItemByPosition(count - 1);
	if (last != nullptr && last->IsSeparator())
		return;   // …and never two in a row

	menu->AppendSeparator();
}

// A row's picture as a tree and a menu show it — 16 by 16.
wxBitmap ibRowPicture(const wxString& picture)
{
	if (picture.IsEmpty())
		return wxNullBitmap;
	wxBitmap bitmap = ibProtocolPicture(picture);
	if (bitmap.IsOk() && bitmap.GetSize() != wxSize(16, 16))
		bitmap = wxBitmap(bitmap.ConvertToImage().Rescale(16, 16, wxIMAGE_QUALITY_HIGH));
	return bitmap;
}

// The objects' ids under `row` — what a new one is told apart from.
void ibObjectIds(const ibProtocolNode& row, std::set<long long>& ids)
{
	if (row.GetId() > 0)
		ids.insert(row.GetId());
	for (const ibProtocolNode& child : row.Children())
		ibObjectIds(child, ids);
}

// The places of the rows under `row` (at `path`) — what an external file's tree opens the first time.
void ibRowPaths(const ibProtocolNode& row, const wxString& path, std::set<wxString>& paths)
{
	for (const ibProtocolNode& child : row.Children()) {
		const wxString childPath = path + wxString::Format(wxT("/%lld"), child.GetId());
		paths.insert(childPath);
		ibRowPaths(child, childPath, paths);
	}
}

// The place of the first object under `row` not among `drawn` — empty: none is.
wxString ibCreatedPath(const ibProtocolNode& row, const wxString& path, const std::set<long long>& drawn)
{
	if (row.GetId() > 0 && drawn.find(row.GetId()) == drawn.end())
		return path;
	for (const ibProtocolNode& child : row.Children()) {
		const wxString created = ibCreatedPath(child, path + wxString::Format(wxT("/%lld"), child.GetId()), drawn);
		if (!created.IsEmpty())
			return created;
	}
	return wxString();
}

} // namespace

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaTreeBase::Draw(const ibProtocolNode& root)
{
	const ibTreeRow* const selected = GetRow(m_metaTreeCtrl->GetSelection());
	const wxString selectedPath = selected != nullptr ? selected->m_path : wxString();

	const bool shown = root.IsNode() && root.GetId() != 0;
	m_root = shown ? root.Clone() : ibProtocolNode();

	// The rows' pictures asked of the server at once — not a call per row drawn.
	ibProtocolPictureReceive(m_root);

	// ⭐ FROZEN FOR THE WHOLE OF IT — the control is what freezes, not the panel around it (the desktop's).
	wxWindowUpdateLocker freeze(m_metaTreeCtrl);
	m_drawing = true;

	// Another metadata — another root: drawn anew.
	wxTreeItemId rootItem = m_metaTreeCtrl->GetRootItem();
	const ibTreeRow* const drawn = GetRow(rootItem);
	if (!shown || drawn == nullptr || drawn->m_row.GetId() != m_root.GetId()) {
		m_metaTreeCtrl->DeleteAllItems();
		rootItem = wxTreeItemId();
	}

	// A configuration's tree shows its root, in the frame or in a tab; an external file's hides it (the ctor's).
	const bool configuration = !m_metaTreeCtrl->HasFlag(wxTR_HIDE_ROOT);

	bool drawnAnew = false;
	if (shown) {
		drawnAnew = !rootItem.IsOk();
		const wxString path = wxString::Format(wxT("%lld"), m_root.GetId());
		if (drawnAnew) {
			// THE FIRST DRAWING OPENS — the desktop's fill did on its way out: the configuration's the root and Common, an
			// external file's (its root hidden) everything.
			if (configuration) {
				m_expanded.insert(path);
				m_expanded.insert(path + wxT("/-1"));
			}
			else {
				ibRowPaths(m_root, path, m_expanded);
			}
			const int image = GetImage(m_root.GetString(ibProtocol::ibSchema::ibMetaData::Picture));
			rootItem = m_metaTreeCtrl->AddRoot(m_root.GetString(ibProtocol::ibSchema::ibMetaData::Title), image, image, new ibTreeRow(m_root, path));
			// …the configuration's in bold (the desktop's); an external file's is hidden.
			if (configuration)
				m_metaTreeCtrl->SetItemBold(rootItem);
		}
		DrawRow(rootItem, m_root);

		// …the selection where it was, while its row is there; drawn anew, the root selected — an external file's first
		// group (its Attributes), as the desktop's fill did.
		const wxTreeItemId item = drawnAnew ? (configuration ? rootItem : FindItem(rootItem, path + wxT("/-1")))
			: selectedPath.IsEmpty() ? wxTreeItemId() : FindItem(rootItem, selectedPath);
		if (item.IsOk() && item != m_metaTreeCtrl->GetSelection())
			m_metaTreeCtrl->SelectItem(item);
	}

	m_drawing = false;

	// A selection the rows moved — its row taken away, the next one selected in its place — is told as a click is.
	const ibTreeRow* const selection = GetRow(m_metaTreeCtrl->GetSelection());
	if (drawnAnew || (selection != nullptr ? selection->m_path : wxString()) != selectedPath)
		SelectItem();
	else
		UpdateToolbar();
}

wxTreeItemId ibMetaTreeBase::InsertRow(const wxTreeItemId& parent, size_t position, const ibProtocolNode& row,
	const wxString& parentPath)
{
	const wxString path = parentPath + wxString::Format(wxT("/%lld"), row.GetId());
	const int image = GetImage(row.GetString(ibProtocol::ibSchema::ibMetaData::Picture));
	const wxTreeItemId item = m_metaTreeCtrl->InsertItem(parent, position, row.GetString(ibProtocol::ibSchema::ibMetaData::Title),
		image, image, new ibTreeRow(row, path));
	DrawRow(item, row);
	return item;
}

void ibMetaTreeBase::DrawRow(const wxTreeItemId& item, const ibProtocolNode& row)
{
	ibTreeRow* const data = static_cast<ibTreeRow*>(m_metaTreeCtrl->GetItemData(item));
	data->m_row = row;

	// A HIDDEN ROOT (an external file's) is not shown: no title or picture to draw — wx keeps none for it, and asking
	// asserts — and never opened or closed: its rows are the tree.
	const bool hiddenRoot = m_metaTreeCtrl->HasFlag(wxTR_HIDE_ROOT) && item == m_metaTreeCtrl->GetRootItem();
	if (!hiddenRoot) {
		const wxString title = row.GetString(ibProtocol::ibSchema::ibMetaData::Title);
		if (m_metaTreeCtrl->GetItemText(item) != title)
			m_metaTreeCtrl->SetItemText(item, title);
		const int image = GetImage(row.GetString(ibProtocol::ibSchema::ibMetaData::Picture));
		if (m_metaTreeCtrl->GetItemImage(item) != image) {
			m_metaTreeCtrl->SetItemImage(item, image, wxTreeItemIcon_Normal);
			m_metaTreeCtrl->SetItemImage(item, image, wxTreeItemIcon_Selected);
		}
	}

	// ITS CHILDREN, MATCHED BY THEIR IDS in the rows' order — a row shown is drawn over, one not shown put in, an item no
	// row stands for any more taken out. One moved is put in at its new place, and goes from its old one.
	std::vector<ibProtocolNode> rows;
	std::set<long long> ids;
	for (const ibProtocolNode& child : row.Children()) {
		if (!IsFound(child))
			continue;
		rows.push_back(child);
		ids.insert(child.GetId());
	}

	std::vector<wxTreeItemId> items;
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_metaTreeCtrl->GetFirstChild(item, cookie); child.IsOk();
		child = m_metaTreeCtrl->GetNextChild(item, cookie))
		items.push_back(child);

	size_t next = 0;
	for (size_t position = 0; position < rows.size(); ++position) {
		const long long id = rows[position].GetId();
		while (next < items.size() && ids.find(GetRow(items[next])->m_row.GetId()) == ids.end())
			m_metaTreeCtrl->Delete(items[next++]);
		if (next < items.size() && GetRow(items[next])->m_row.GetId() == id)
			DrawRow(items[next++], rows[position]);
		else
			InsertRow(item, position, rows[position], data->m_path);
	}
	while (next < items.size())
		m_metaTreeCtrl->Delete(items[next++]);

	if (hiddenRoot)
		return;

	// OPEN AS THE PERSON LEFT IT — and while searching, everything: a match three levels down is not an answer until it
	// can be seen.
	const bool expanded = m_search.IsEmpty() ? m_expanded.find(data->m_path) != m_expanded.end() : true;
	if (expanded && m_metaTreeCtrl->ItemHasChildren(item) && !m_metaTreeCtrl->IsExpanded(item))
		m_metaTreeCtrl->Expand(item);
	else if (!expanded && m_metaTreeCtrl->IsExpanded(item))
		m_metaTreeCtrl->Collapse(item);
}

bool ibMetaTreeBase::IsFound(const ibProtocolNode& row) const
{
	if (m_search.IsEmpty())
		return true;

	if (row.GetId() > 0) {
		const ibString needle = m_search.Lower();
		if (row.GetString(ibProtocol::ibSchema::ibMetaData::Title).Lower().Find(needle) != wxNOT_FOUND
			|| row.GetString(ibProtocol::ibSchema::ibMetaData::Synonym).Lower().Find(needle) != wxNOT_FOUND)
			return true;
	}

	for (const ibProtocolNode& child : row.Children()) {
		if (IsFound(child))
			return true;
	}
	return false;
}

int ibMetaTreeBase::GetImage(const wxString& picture)
{
	const auto found = m_images.find(picture);
	if (found != m_images.end())
		return found->second;

	// A ROW WITH NO PICTURE OF ITS OWN IS A FOLDER — Common, the desktop's wxART_COMMON_FOLDER.
	const wxBitmap bitmap = picture.IsEmpty()
		? wxArtProvider::GetBitmapBundle(wxART_COMMON_FOLDER, wxART_METATREE).GetBitmap(wxSize(16, 16))
		: ibRowPicture(picture);
	const int image = bitmap.IsOk() ? m_metaTreeCtrl->GetImageList()->Add(bitmap) : wxNOT_FOUND;
	m_images.emplace(picture, image);
	return image;
}

wxTreeItemId ibMetaTreeBase::FindItem(const wxTreeItemId& item, const wxString& path) const
{
	const ibTreeRow* const row = GetRow(item);
	if (row == nullptr)
		return wxTreeItemId();
	if (row->m_path == path)
		return item;
	if (!path.StartsWith(row->m_path + wxT("/")))
		return wxTreeItemId();

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_metaTreeCtrl->GetFirstChild(item, cookie); child.IsOk();
		child = m_metaTreeCtrl->GetNextChild(item, cookie)) {
		const wxTreeItemId found = FindItem(child, path);
		if (found.IsOk())
			return found;
	}
	return wxTreeItemId();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const ibMetaTreeBase::ibTreeRow* ibMetaTreeBase::GetRow(const wxTreeItemId& item) const
{
	return item.IsOk() ? static_cast<const ibTreeRow*>(m_metaTreeCtrl->GetItemData(item)) : nullptr;
}

long long ibMetaTreeBase::GetObject(const wxTreeItemId& item) const
{
	const ibTreeRow* const row = GetRow(item);
	return row != nullptr && row->m_row.GetId() > 0 ? row->m_row.GetId() : 0;
}

bool ibMetaTreeBase::GetPlace(const wxTreeItemId& item, wxString& type, long long& parent) const
{
	wxTreeItemId at = item;
	for (; at.IsOk(); at = m_metaTreeCtrl->GetItemParent(at)) {
		const ibTreeRow* const row = GetRow(at);
		if (row != nullptr && row->m_row.Has(ibProtocol::ibSchema::ibMetaData::Group))
			break;
	}
	if (!at.IsOk())
		return false;

	type = GetRow(at)->m_row.GetString(ibProtocol::ibSchema::ibMetaData::Group);
	parent = 0;
	for (const wxTreeItemId root = m_metaTreeCtrl->GetRootItem(); at.IsOk() && at != root; at = m_metaTreeCtrl->GetItemParent(at)) {
		parent = GetObject(at);
		if (parent != 0)
			break;
	}
	return true;
}

ibProtocolNode ibMetaTreeBase::GetParams(ibProtocol::ibSchema::ibMetaData::ibCommand command, ibProtocolNode args) const
{
	if (m_tab != 0)
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Tab, m_tab);

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::MetaData));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(command));
	params.SetValue(ibProtocol::ibSchema::Args, args);
	return params;
}

bool ibMetaTreeBase::Command(ibProtocol::ibSchema::ibMetaData::ibCommand command, ibProtocolNode args) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode answer;
	return frame != nullptr && frame->Call(ibProtocol::ibMethod::Schema, GetParams(command, args), answer);
}

void ibMetaTreeBase::UpdateChoiceSelection(wxChoice* choice, const ibProtocolNode& property)
{
	choice->Clear();
	choice->AppendString(_("<not selected>"));

	int defSelection = 0;

	const long long value = property.GetInt(ibProtocol::ibSchema::ibProperties::Value);
	for (const ibProtocolNode& offered : property.Children()) {
		const long long id = offered.GetInt(ibProtocol::ibSchema::ibProperties::Id);
		const int selection_id = choice->Append(offered.GetString(ibProtocol::ibSchema::ibProperties::Title),
			reinterpret_cast<void*>(static_cast<intptr_t>(id)));
		if (id == value)
			defSelection = selection_id;
	}

	choice->SetSelection(defSelection);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaTreeBase::CreateItem()
{
	if (m_readOnly)
		return;

	wxString type;
	long long parent = 0;
	if (!GetPlace(m_metaTreeCtrl->GetSelection(), type, parent))
		return;

	// ASK — the new object's row comes back with the frame and is selected, the inspector showing it — its rows came
	// with the same answer (the frame's Properties), nothing is read; then its editor is opened, as the desktop's click
	// did (NewItem, then OpenObjectForm): made while the row is shown, it shows its own.
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Type, type);
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Parent, parent);
	std::set<long long> drawn;
	ibObjectIds(m_root, drawn);
	if (Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Create, args) && SelectCreated(drawn))
		EditItem();
}

// WHETHER OPEN OPENS ANYTHING — the root says which kinds have an editor (Editors). A catalog's or a document's Edit
// opened nothing and cost an exchange; a server that does not say is asked, as before.
static bool ibOpensEditor(const ibProtocolNode& root, const ibProtocolNode& row)
{
	const ibProtocolNode editors = root.FindChild(ibProtocol::ibSchema::ibMetaData::Editors);
	if (!editors.IsNode())
		return true;
	const wxString type = row.GetString(ibProtocol::ibSchema::ibMetaData::Type);
	for (const ibProtocolNode& editor : editors.Children()) {
		if (editor.GetString(ibProtocol::ibSchema::ibMetaData::Type) == type)
			return true;
	}
	return false;
}

void ibMetaTreeBase::EditItem()
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	const long long id = GetObject(selection);
	if (id == 0 || !ibOpensEditor(m_root, GetRow(selection)->m_row))
		return;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, id);
	Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Open, args);
}

void ibMetaTreeBase::EditModule(const ibGuid& moduleName, int line, bool setRunLine)
{
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Module, moduleName.str());
	if (!Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Open, args))
		return;

	// The tab is drawn by now — the frame came with the answer; its module found by the address its State says.
	for (ibFrontendDocument* const doc : docManager->GetDocumentsVector()) {
		ibModuleEditDocument* const moduleDoc = dynamic_cast<ibModuleEditDocument*>(doc);
		if (moduleDoc != nullptr && ibGuid(moduleDoc->GetDocPath()) == moduleName) {
			moduleDoc->SetCurrentLine(line, setRunLine);
			return;
		}
	}
}

void ibMetaTreeBase::RemoveItem()
{
	if (m_readOnly)
		return;

	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	const long long id = GetObject(selection);
	if (id == 0 || selection == m_metaTreeCtrl->GetRootItem())
		return;

	// ASK, then let the answer come back — the row goes with the frame, the editors over it with the server's Removed.
	// ⭐ …and the row the tree goes to — the next one, else the one before, else the parent: its rows shown with the same
	// answer (Select, the frame's Properties), so it is selected without reading them.
	wxTreeItemId next = m_metaTreeCtrl->GetNextSibling(selection);
	if (!next.IsOk())
		next = m_metaTreeCtrl->GetPrevSibling(selection);
	if (!next.IsOk())
		next = m_metaTreeCtrl->GetItemParent(selection);
	const long long select = next.IsOk() ? GetObject(next) : 0;
	const ibTreeRow* const nextRow = next.IsOk() ? GetRow(next) : nullptr;
	const wxString nextPath = nextRow != nullptr ? nextRow->m_path : wxString();
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, id);
	if (select != 0)
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Select, select);
	if (!Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Remove, args) || nextPath.IsEmpty())
		return;

	// The frame drew the tree anew without the old row, the parent selected in its place: the row it goes to selected
	// again; else, the tree selected it as the old one went — shown again.
	const wxTreeItemId item = FindItem(m_metaTreeCtrl->GetRootItem(), nextPath);
	if (item.IsOk() && item != m_metaTreeCtrl->GetSelection())
		m_metaTreeCtrl->SelectItem(item);
	else
		ActivateTree();
}

void ibMetaTreeBase::PropertyItem()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return;

	if (!frame->IsShownInspector())
		frame->ShowInspector();

	ActivateTree();
}

void ibMetaTreeBase::SelectItem()
{
	UpdateToolbar();

	// WHAT THE INSPECTOR SHOWS — the row's object, made as the row is selected, what the server says of it asked as it is
	// shown; a group row shows nothing.
	const long long id = GetObject(m_metaTreeCtrl->GetSelection());
	std::shared_ptr<ibValueMetaObject> object;
	if (id != 0)
		object = std::make_shared<ibValueMetaObject>(static_cast<ibMetaID>(id), ibMetaData(m_tab));
	m_object = std::move(object);
	ActivateTree();
}

void ibMetaTreeBase::ActivateTree()
{
	// ⭐ SHOWN ONCE THE EVENT THAT ACTIVATED IT IS DONE, the object held by then: a click on another row as the focus
	// comes to the tree selects it on the way, and the inspector is asked once, for that row — it skips the one it shows.
	CallAfter([this]() {
		// Hidden, the inspector holds it all the same — Properties shows what it holds.
		ibObjectInspector* const inspector = objectInspector;
		if (inspector != nullptr)
			inspector->SelectObject(m_object.get());
	});
}

void ibMetaTreeBase::UpItem()
{
	if (m_readOnly)
		return;

	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (selection.IsOk())
		MoveItem(selection, m_metaTreeCtrl->GetPrevSibling(selection));
}

void ibMetaTreeBase::DownItem()
{
	if (m_readOnly)
		return;

	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (selection.IsOk())
		MoveItem(selection, m_metaTreeCtrl->GetNextSibling(selection));
}

void ibMetaTreeBase::MoveItem(const wxTreeItemId& item, const wxTreeItemId& sibling)
{
	const long long id = GetObject(item);
	if (id == 0 || GetObject(sibling) == 0 || item == m_metaTreeCtrl->GetRootItem())
		return;

	// The door moves it and announces `Moved`; the rows follow.
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, id);
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Position, GetRow(sibling)->m_row.GetInt(ibProtocol::ibSchema::ibMetaData::Position));
	Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Move, args);
}

// A NEW OBJECT IS LEFT SELECTED AND OPENED, as the desktop's create and paste left it — its row the one the frame brought
// back that was not drawn before. Opened first, so the shape is final before the selection aims at it.
bool ibMetaTreeBase::SelectCreated(const std::set<long long>& drawn)
{
	const wxTreeItemId root = m_metaTreeCtrl->GetRootItem();
	const ibTreeRow* const row = GetRow(root);
	if (row == nullptr)
		return false;

	const wxString path = ibCreatedPath(m_root, row->m_path, drawn);
	const wxTreeItemId item = path.IsEmpty() ? wxTreeItemId() : FindItem(root, path);
	if (!item.IsOk())
		return false;

	m_metaTreeCtrl->Expand(item);
	m_metaTreeCtrl->SelectItem(item);
	return true;
}

// The sort button: the group's objects by name, each put where the object at its place in the group stands now (the
// desktop's SortItemsByName) — the places read from the rows each move brings back.
void ibMetaTreeBase::SortItem()
{
	if (m_readOnly)
		return;

	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (GetObject(selection) == 0 || selection == m_metaTreeCtrl->GetRootItem())
		return;

	const wxString parentPath = GetRow(m_metaTreeCtrl->GetItemParent(selection))->m_path;
	const auto objects = [this, &parentPath]() {
		std::vector<ibProtocolNode> rows;
		const wxTreeItemId parentItem = FindItem(m_metaTreeCtrl->GetRootItem(), parentPath);
		wxTreeItemIdValue cookie;
		for (wxTreeItemId row = parentItem.IsOk() ? m_metaTreeCtrl->GetFirstChild(parentItem, cookie) : wxTreeItemId();
			row.IsOk(); row = m_metaTreeCtrl->GetNextChild(parentItem, cookie)) {
			if (GetObject(row) != 0)
				rows.push_back(GetRow(row)->m_row);
		}
		std::sort(rows.begin(), rows.end(), [](const ibProtocolNode& a, const ibProtocolNode& b) {
			return a.GetInt(ibProtocol::ibSchema::ibMetaData::Position) < b.GetInt(ibProtocol::ibSchema::ibMetaData::Position);
		});
		return rows;
	};

	std::vector<ibProtocolNode> sorted = objects();
	std::stable_sort(sorted.begin(), sorted.end(), [](const ibProtocolNode& a, const ibProtocolNode& b) {
		return wxStrcmp(a.GetString(ibProtocol::ibSchema::ibMetaData::Title), b.GetString(ibProtocol::ibSchema::ibMetaData::Title)) < 0;
	});

	for (size_t i = 0; i < sorted.size(); ++i) {
		const std::vector<ibProtocolNode> placed = objects();
		if (i >= placed.size())
			return;
		if (placed[i].GetId() == sorted[i].GetId())
			continue;

		ibProtocolNode args;
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, sorted[i].GetId());
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Position, placed[i].GetInt(ibProtocol::ibSchema::ibMetaData::Position));
		if (!Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Move, args))
			return;
	}
}

// THE COPY IS THE SERVER'S — the object's bytes written into a file uploaded empty and taken down from it, then onto the
// clipboard as the desktop put them.
void ibMetaTreeBase::CopyItem()
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	const long long id = GetObject(selection);
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (id == 0 || selection == m_metaTreeCtrl->GetRootItem() || frame == nullptr)
		return;

	const wxString title = GetRow(selection)->m_row.GetString(ibProtocol::ibSchema::ibMetaData::Title);
	const wxString local = wxFileName::CreateTempFileName(wxT("oes"));
	if (local.IsEmpty())
		return;

	ibMemoryBuffer bytes;
	wxString file;
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, id);
	if (frame->UploadFile(local, file)) {
		args.SetValue(ibProtocol::ibSchema::ibMetaData::File, file);
		if (Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Copy, args) && frame->DownloadFile(file, local)) {
			wxFile read(local);
			const wxFileOffset length = read.IsOpened() ? read.Length() : 0;
			if (length > 0) {
				void* const buffer = bytes.GetWriteBuf(static_cast<size_t>(length));
				const ssize_t done = read.Read(buffer, static_cast<size_t>(length));
				bytes.UngetWriteBuf(done == length ? static_cast<size_t>(length) : 0);
			}
		}
	}
	wxRemoveFile(local);

	if (bytes.IsEmpty())
		return;

	const ibClipboardLock clipboard;   // closes on every path out — see clipboardLock.h
	if (!clipboard.IsOpen())
		return;

	wxDataObjectComposite* composite_object = new wxDataObjectComposite;
	wxCustomDataObject* custom_object = new wxCustomDataObject(oes_clipboard_metadata);
	custom_object->SetData(bytes.GetDataLen(), bytes.GetData());

	composite_object->Add(custom_object);
	composite_object->Add(new wxTextDataObject(title), true);

	// tell clipboard
	wxTheClipboard->SetData(composite_object);
}

// …and back: the clipboard's bytes uploaded, read by the server where the selection says — the class and the object.
void ibMetaTreeBase::PasteItem()
{
	if (m_readOnly)
		return;

	wxString type;
	long long parent = 0;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || !GetPlace(m_metaTreeCtrl->GetSelection(), type, parent))
		return;

	wxCustomDataObject data(oes_clipboard_metadata);
	{
		const ibClipboardLock clipboard;
		if (!clipboard.IsOpen() || !wxTheClipboard->IsSupported(oes_clipboard_metadata) || !wxTheClipboard->GetData(data))
			return;
	}

	const wxString local = wxFileName::CreateTempFileName(wxT("oes"));
	if (local.IsEmpty())
		return;

	bool written = false;
	{
		wxFile write(local, wxFile::write);
		written = write.IsOpened() && write.Write(data.GetData(), data.GetDataSize()) == data.GetDataSize();
	}

	wxString file;
	if (written && frame->UploadFile(local, file)) {
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Type, type);
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Parent, parent);
		args.SetValue(ibProtocol::ibSchema::ibMetaData::File, file);
		std::set<long long> drawn;
		ibObjectIds(m_root, drawn);
		if (Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Paste, args))
			SelectCreated(drawn);
	}
	wxRemoveFile(local);
}

// A DATA PROCESSOR OR A REPORT FROM THE FILE IT IS KEPT IN — the desktop's Replace and Insert: the file the person picks,
// uploaded, read by the server over the selected object (Id), or into the selected group (Type, Parent); the row it made
// selected.
void ibMetaTreeBase::LoadItem(const wxString& extension)
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	ibFileKind kind = ibFileKind::Tool;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (m_readOnly || frame == nullptr || !ibFileKindOf(wxT("*.") + extension, kind))
		return;

	ibProtocolNode args;
	const long long id = GetObject(selection);
	if (id != 0) {
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, id);
	}
	else {
		wxString type;
		long long parent = 0;
		if (!GetPlace(selection, type, parent))
			return;
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Type, type);
		args.SetValue(ibProtocol::ibSchema::ibMetaData::Parent, parent);
	}

	wxFileDialog openFileDialog(this, kind == ibFileKind::Tool ? _("Open data processor file") : _("Open report file"), "", "",
		ibFileFilter(kind), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (openFileDialog.ShowModal() == wxID_CANCEL)
		return;

	wxString file;
	if (!frame->UploadFile(openFileDialog.GetPath(), file))
		return;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::File, file);
	std::set<long long> drawn;
	ibObjectIds(m_root, drawn);
	if (Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Load, args))
		SelectCreated(drawn);
}

// …and written into one — the desktop's Save: into a file uploaded empty, taken down to where the person chose (the
// configuration's SaveConfigToFile).
void ibMetaTreeBase::UnloadItem(const wxString& extension)
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	const long long id = GetObject(selection);
	ibFileKind kind = ibFileKind::Tool;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (id == 0 || frame == nullptr || !ibFileKindOf(wxT("*.") + extension, kind))
		return;

	wxFileDialog saveFileDialog(this, kind == ibFileKind::Tool ? _("Save data processor file") : _("Save report file"), "", "",
		ibFileFilter(kind), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	saveFileDialog.SetFilename(GetRow(selection)->m_row.GetString(ibProtocol::ibSchema::ibMetaData::Title));
	if (saveFileDialog.ShowModal() == wxID_CANCEL)
		return;

	const wxString local = wxFileName::CreateTempFileName(wxT("oes"));
	if (local.IsEmpty())
		return;
	wxString file;
	const bool uploaded = frame->UploadFile(local, file);
	wxRemoveFile(local);

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, id);
	args.SetValue(ibProtocol::ibSchema::ibMetaData::File, file);
	if (uploaded && Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Unload, args))
		frame->DownloadFile(file, saveFileDialog.GetPath());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaTreeBase::UpdateToolbar()
{
	const wxTreeItemId item = m_metaTreeCtrl->GetSelection();
	const wxTreeItemId root = m_metaTreeCtrl->GetRootItem();
	const ibTreeRow* const row = GetRow(item);
	// Common — the configuration root's first row, -1 by its place (an external file's is its Attributes).
	const bool common = !m_metaTreeCtrl->HasFlag(wxTR_HIDE_ROOT) && row != nullptr && row->m_row.GetId() == -1
		&& m_metaTreeCtrl->GetItemParent(item) == root;
	const bool object = GetObject(item) != 0 && item != root;

	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, !m_readOnly && row != nullptr && item != root && !common);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, object);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DELETE, !m_readOnly && object);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, !m_readOnly && object);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, !m_readOnly && object);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, !m_readOnly && object);

	m_metaTreeToolbar->Refresh();
}

bool ibMetaTreeBase::AppendMetaMenu(wxMenu& menu, const ibProtocolNode& row)
{
	const ibProtocolNode items = row.FindChild(ibProtocol::ibSchema::ibMetaData::Menu);
	const bool standard = items.GetBool(ibProtocol::ibSchema::ibMetaData::Standard, true);

	const std::vector<ibProtocolNode> entries = items.Children();
	if (entries.empty())
		return standard;

	// The entries' pictures asked for at once — not a call per entry.
	ibProtocolPictureReceive(items);

	for (size_t idx = 0; idx < entries.size(); idx++) {

		const ibProtocolNode& entry = entries[idx];

		// THE LINE GOES WHERE THE KIND CHANGES — never where somebody asked for one.
		if (idx > 0 && entries[idx - 1].GetInt(ibProtocol::ibSchema::ibMetaData::Kind) != entry.GetInt(ibProtocol::ibSchema::ibMetaData::Kind))
			ibAppendSeparator(&menu);

		const int id = wxWindow::NewControlId();
		wxMenuItem* menuItem = menu.Append(id, entry.GetString(ibProtocol::ibSchema::ibMetaData::Title));

		const long long object = entry.GetInt(ibProtocol::ibSchema::ibMetaData::Object);
		const wxBitmap bitmap = ibRowPicture(entry.GetString(ibProtocol::ibSchema::ibMetaData::Picture));
		if (bitmap.IsOk())
			menuItem->SetBitmap(bitmap);

		// ⭐⭐ THE ITEM CARRIES ITS OWN ACTION — what it opens, by its id (a module's carries its Command too). A modal
		// editor — a Command with nothing to open — is the tree's, over the row's object (the engine's ProcessCommand
		// called it so), by the item's Name, and so is the file a data processor or a report is kept in (Load, Unload —
		// the file by its Extension); one this tree does not have: shown, shut.
		if (object == 0) {
			const wxString name = entry.GetString(ibProtocol::ibSchema::ibMetaData::Name);
			const ibMetaID owner = static_cast<ibMetaID>(row.GetId());
			if (name == wxT("PredefinedValues")) {
				menu.Bind(wxEVT_MENU, [this, owner](wxCommandEvent&) {
					ibValueMetaObjectRecordDataHierarchyMutableRef metaObject(owner, ibMetaData(m_tab));
					EditPredefinedValues(&metaObject);
				}, id);
			}
			else if (name == wxT("HomePage")) {
				menu.Bind(wxEVT_MENU, [this, owner](wxCommandEvent&) {
					ibValueMetaObjectConfiguration metaObject(owner, ibMetaData(m_tab));
					EditHomePage(&metaObject);
				}, id);
			}
			else if (name == wxT("Load")) {
				const wxString extension = entry.GetString(ibProtocol::ibSchema::ibMetaData::Extension);
				menu.Bind(wxEVT_MENU, [this, extension](wxCommandEvent&) { LoadItem(extension); }, id);
				menuItem->Enable(!m_readOnly);
			}
			else if (name == wxT("Unload")) {
				const wxString extension = entry.GetString(ibProtocol::ibSchema::ibMetaData::Extension);
				menu.Bind(wxEVT_MENU, [this, extension](wxCommandEvent&) { UnloadItem(extension); }, id);
			}
			else {
				menuItem->Enable(false);
			}
			continue;
		}
		menu.Bind(wxEVT_MENU, [this, object](wxCommandEvent&) {
			ibProtocolNode args;
			args.SetValue(ibProtocol::ibSchema::ibMetaData::Id, object);
			Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Open, args);
		}, id);
	}

	// …and the boundary to the tree's OWN block below.
	ibAppendSeparator(&menu);

	return standard;
}

void ibMetaTreeBase::ShowContextMenu(const wxTreeItemId& item, const wxPoint& pos)
{
	const ibTreeRow* const row = GetRow(item);
	if (row == nullptr)
		return;

	wxMenu menu;   // stack — PopupMenu does not take ownership, and it blocks until dismissed

	if (row->m_row.GetId() > 0) {
		// ⭐ ASKED ONCE. The object offers what it opens, this draws it, and the answer says whether the standard New /
		// Edit / Remove block applies.
		if (AppendMetaMenu(menu, row->m_row)) {
			wxMenuItem* menuItem = nullptr;
			menuItem = menu.Append(ID_METATREE_NEW, _("New"));
			menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)));
			menuItem->Enable(!m_readOnly);
			menuItem = menu.Append(ID_METATREE_EDIT, _("Edit"));
			menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_EDIT, wxART_FRONTEND, wxSize(16, 16)));
			menuItem = menu.Append(ID_METATREE_DELETE, _("Remove"));
			menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_FRONTEND, wxSize(16, 16)));
			menuItem->Enable(!m_readOnly);
		}

		// ⭐ THE TWO TEXTS AND PROPERTIES ALWAYS APPLY — every object has them, the configuration root included. The texts
		// are editors, shut in a read-only configuration as New and Remove are (the desktop's AppendTextsMenu).
		ibAppendSeparator(&menu);
		menu.Append(ID_METATREE_HELP, _("Help information..."))->Enable(!m_readOnly);
		menu.Append(ID_METATREE_NOTES, _("Technical information..."))->Enable(!m_readOnly);
		menu.AppendSeparator();
		wxMenuItem* propertyItem = menu.Append(ID_METATREE_PROPERTY, _("Properties"));
		propertyItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));
	}
	else if (m_metaTreeCtrl->HasFlag(wxTR_HIDE_ROOT) || row->m_row.GetId() != -1
		|| m_metaTreeCtrl->GetItemParent(item) != m_metaTreeCtrl->GetRootItem()) {
		// …a group's own first — one read in from a file (Load), where its kind is kept in one.
		AppendMetaMenu(menu, row->m_row);
		wxMenuItem* menuItem = menu.Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)));
		menuItem->Enable(!m_readOnly);
	}

	if (menu.GetMenuItemCount() > 0)
		m_metaTreeCtrl->PopupMenu(&menu, pos);
}

void ibMetaTreeBase::Search(const wxString& search)
{
	if (search == m_search)
		return;

	// The rows drawn again through the text — what it keeps, opened; none, as the person left them.
	m_search = search;
	Draw(m_root);

	// …and the root selected, as the desktop's fill after a search did.
	const wxTreeItemId root = m_metaTreeCtrl->GetRootItem();
	if (root.IsOk())
		m_metaTreeCtrl->SelectItem(root);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibConfigurationTree::IsModified() const
{
	return m_root.GetBool(ibProtocol::ibSchema::ibMetaData::Modified);
}

bool ibConfigurationTree::IsEdited() const
{
	return m_root.GetBool(ibProtocol::ibSchema::ibMetaData::Edited);
}

wxString ibConfigurationTree::GetConfigName() const
{
	return m_root.GetString(ibProtocol::ibSchema::ibMetaData::Title);
}

bool ibConfigurationTree::SaveDatabase() const
{
	return Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Save, ibProtocolNode());
}

bool ibConfigurationTree::RollbackConfiguration() const
{
	return Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Rollback, ibProtocolNode());
}

bool ibConfigurationTree::ApplyConfiguration() const
{
	return Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Apply, ibProtocolNode());
}

bool ibConfigurationTree::LoadConfigFromFile(const wxString& strFileName) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	wxString file;
	if (frame == nullptr || !frame->UploadFile(strFileName, file))
		return false;

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::File, file);
	return Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Load, args);
}

// THE FILE IT IS WRITTEN INTO IS THE SERVER'S — a temporary one, made by uploading an empty file of this computer's
// (the tree's Copy): the server writes the configuration into it, and it is taken down to where the person chose.
bool ibConfigurationTree::SaveConfigToFile(const wxString& strFileName) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	const wxString local = frame != nullptr ? wxFileName::CreateTempFileName(wxT("oes")) : wxString();
	if (local.IsEmpty())
		return false;

	wxString file;
	const bool uploaded = frame->UploadFile(local, file);
	wxRemoveFile(local);

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibSchema::ibMetaData::File, file);
	return uploaded
		&& Command(ibProtocol::ibSchema::ibMetaData::ibCommand::Unload, args)
		&& frame->DownloadFile(file, strFileName);
}
