////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — designer menu & icon
////////////////////////////////////////////////////////////////////////////

#include "parameterizedJob.h"

#include "backend/metaData.h"
#include "backend/backend_picture.h"   // ibBackendPicture::GetIconFromBase64 / GetPicture

bool ibValueMetaObjectParameterizedJob::PrepareContextMenu(wxMenu* defaultMenu)
{
	wxMenuItem* menuItem = nullptr;
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MODULE, _("Open object module"));
	menuItem->SetBitmap((*m_propertyObjectModule)->GetIcon());
	// The MANAGER module is where JobProcessing lives — the entry the scheduler calls. It is the
	// one a developer opens most, which is why it is named for what it holds rather than for where
	// it sits.
	menuItem = defaultMenu->Append(ID_METATREE_OPEN_MANAGER, _("Open job module"));
	menuItem->SetBitmap((*m_propertyManagerModule)->GetIcon());
	defaultMenu->AppendSeparator();
	return false;
}

void ibValueMetaObjectParameterizedJob::ProcessCommand(unsigned int id)
{
	ibBackendMetadataTree* metaTree = m_metaData->GetMetaTree();
	wxASSERT(metaTree);

	if (id == ID_METATREE_OPEN_MODULE)
		metaTree->OpenObjectForm(m_propertyObjectModule->GetMetaObject());
	else if (id == ID_METATREE_OPEN_MANAGER)
		metaTree->OpenObjectForm(m_propertyManagerModule->GetMetaObject());
}

/* PNG — the same clock the predefined kind wears: one subsystem, one visual. */
static const wxString s_parameterizedJob_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAALPSURBVDhPXZBdTFJhGMffttStu7zoyuvWZtO2blrljemspRnucJqeEBBnpuiUtNI0BylaiGVwgUZpS8lWAWkaYh9TK52bk6V8tcKkjqKi0gpmYj7tPcZx8N+e7fn4/94974PQtnYLryipsoa20sKa1kP/e4jfrBPjCNVF1erEsgZtKfYihKKYZlxcUkylXGs2jjrgxdgsCGTtpiSEYp6eSDvzTK4GHD3JpzIPIBQluHFP3zs+C4YRB1Q0aocPplJ7kKCiMdswbIdBCw2XW7psfac5N91Ujo0W8OE7j4IfvPMwLxCAm6JsAyfTpVV3Hk8OfZoH/bAdcsubClDRdXWFfswFV291uvo55BevgA9LMhks9fbCT4sFfBYLLBiNsCyTwYqQDwMcruOa8pHVMD4LF2tVUnSh5m6CUNpuMmcSpjWREOiODgiur0NIM3MesNJe2PyzzszW8vPgbcZZfX79/ZcFtaojzB2GEhKOefi5G7RKzYIh9b0ZBze9yNa0SgUL/NzgYHz80dBxkS0rS+srLwc/TYfBWM9NoxAMbrI19mAvZkL8LjtJzngVijAQy/7VDQbT+8g2YK+DJKcxi9IQiraTpHtFo4n0gbSlE6oVDyLbgL0OkpzDLLOBgyStXqUyzOR3OsE1MQk9r0Zh4N0EbG1tsTNvczN+wMpsgGUliK41iQQCi9vH+u10gm9qigWMgx/A+vkbkwc8HvBJJDBNEF3sEUeOH09eFomAbmtjTBurqywcUvDv9ga0RgNLIhFghn0Ay8Lh9PjFxUDrdLCz7I7wF+jubvCLxYC9YTBWP0LRNpJ4HSgpAY9cDstmM/xyOpnAOe4FxGKwc7lm7I3kUXqDbh/vUlP9GDe71ZWT7cFrrhYXM4Fz3Pt4jlLxJE0yQvlkbySP8qoU8Tl16kScP0QodiIlJWOGwynDMZmamq5CKBbPeHWaw4WVt/eHuH/a5HGZWNTxDAAAAABJRU5ErkJggg==");

wxIcon ibValueMetaObjectParameterizedJob::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectParameterizedJob::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_parameterizedJob_16_png, wxSize(16, 16));

	return icon;
}
