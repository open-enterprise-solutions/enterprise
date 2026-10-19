////////////////////////////////////////////////////////////////////////////
//	Description : configuration browsed read-only — File → Open / Open database configuration
////////////////////////////////////////////////////////////////////////////

#include "docViewMetaFile.h"

#include "backend/backend_picture.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/temp/tempStorage.h"
#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientChildFrame.h"
#include "protocol/protocol.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibConfigurationBrowserView, ibMetaView);
wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataBrowserDocument, ibMetaDataDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataFileDocument, ibMetadataBrowserDocument);

bool ibConfigurationBrowserView::OnCreate(ibDocument* doc, long flags)
{
	ibClientFrame* const frame = GetDocChildFrame() != nullptr ? GetDocChildFrame()->GetFrame() : nullptr;
	m_metaTree = std::make_unique<ibConfigurationTree>(frame);
	return ibView::OnCreate(doc, flags);
}

void ibConfigurationBrowserView::OnDraw(ibDataNode& frame)
{
	if (m_metaTree)
		m_metaTree->OnDraw(frame.Child(ibProtocol::ibView::MetaData));
}

ibPictureDescription ibMetadataBrowserDocument::GetIcon() const
{
	if (m_metaData != nullptr) {
		if (ibValueMetaObject* const metaObject = m_metaData->GetCommonMetaObject())
			return ibPictureDescription(metaObject->GetClassType());
	}
	return ibPictureDescription(g_metaCommonMetadataCLSID);
}

bool ibMetadataBrowserDocument::OnCreate(const wxString& path, long flags)
{
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	if (m_metaData != nullptr && GetMetaTree() != nullptr)
		return GetMetaTree()->Load(m_metaData);
	return true;
}

bool ibMetadataBrowserDocument::OnCloseDocument()
{
	if (m_metaData != nullptr && !m_metaData->CloseDatabase(forceCloseFlag))
		return false;
	return ibMetaDocument::OnCloseDocument();
}

ibConfigurationTree* ibMetadataBrowserDocument::GetMetaTree() const
{
	ibView* const view = GetFirstView();
	const auto* browser = view != nullptr ? dynamic_cast<ibConfigurationBrowserView*>(view) : nullptr;
	return browser != nullptr ? browser->GetMetaTree() : nullptr;
}

bool ibMetadataFileDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

bool ibMetadataFileDocument::DoOpenDocument(const wxString& filename)
{
	// Temp storage id (thin File → Open) or a disk path (desktop).
	wxMemoryBuffer bytes;
	const ibTempFile temp(filename);
	if (temp.IsOpened()) {
		wxMemoryBuffer part;
		for (int index = 0; temp.ReadPart(index, part); ++index)
			bytes.AppendData(part.GetData(), part.GetDataLen());
		if (!m_metaData->LoadConfigFromBuffer(bytes))
			return false;
		SetTitle(temp.GetName());
		SetFilename(temp.GetName(), true);
	}
	else if (!m_metaData->LoadConfigFromFile(filename)) {
		return false;
	}

	m_metaData->SetReadOnly(true);
	if (!m_metaData->RunDatabase(onlyLoadFlag))
		return false;

	if (ibConfigurationTree* const tree = GetMetaTree()) {
		if (!tree->Load(m_metaData))
			return false;
	}

	if (ibValueMetaObject* const root = m_metaData->GetCommonMetaObject()) {
		SetMetaObject(root);
		if (GetTitle().IsEmpty())
			SetTitle(root->GetModuleName());
	}
	return true;
}

bool ibMetadataFileDocument::DoSaveDocument(const wxString& WXUNUSED(filename))
{
	// Read-only browser — nothing to write.
	return true;
}
