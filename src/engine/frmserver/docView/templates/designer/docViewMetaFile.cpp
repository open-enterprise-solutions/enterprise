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

wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataTreeView, ibMetaView);
wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataBrowserDocument, ibMetaDataDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibMetadataFileDocument, ibMetadataBrowserDocument);

bool ibMetadataTreeView::OnCreate(ibDocument* doc, long flags)
{
	ibClientFrame* const frame = GetDocChildFrame() != nullptr ? GetDocChildFrame()->GetFrame() : nullptr;
	m_metaTree = std::make_unique<ibConfigurationTree>(frame);
	return ibView::OnCreate(doc, flags);
}

void ibMetadataTreeView::OnDraw(ibDataNode& frame)
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

ibView* ibMetadataBrowserDocument::DoCreateView()
{
	return new ibMetadataTreeView();
}

ibConfigurationTree* ibMetadataBrowserDocument::GetMetaTree() const
{
	// GUARD THE CAST, not the pointer that went into it — see the twin in docViewDataProcessorEdit.cpp.
	ibMetadataTreeView* view = wxDynamicCast(GetFirstView(), ibMetadataTreeView);
	return view != nullptr ? view->GetMetaTree() : nullptr;
}

bool ibMetadataFileDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

bool ibMetadataFileDocument::OnCloseDocument()
{
	if (!m_metaData->CloseDatabase(forceCloseFlag))
		return false;
	return ibMetaDocument::OnCloseDocument();
}

bool ibMetadataFileDocument::DoOpenDocument(const wxString& filename)
{
	// A FILE OF THE SESSION'S — the one the client uploaded (File → Open), as an external file's is
	// (ibDataProcessorEditDocument): the server opens no path of its own disk on a client's word.
	const ibTempFile temp(filename);
	if (!temp.IsOpened())
		return false;
	ibMemoryBuffer bytes;
	ibMemoryBuffer part;
	for (int index = 0; temp.ReadPart(index, part); ++index)
		bytes.AppendData(part.GetData(), part.GetDataLen());
	if (!m_metaData->LoadConfigFromBuffer(bytes))
		return false;
	SetTitle(temp.GetName());
	SetFilename(temp.GetName(), true);

	m_metaData->SetReadOnly(true);
	if (!m_metaData->RunDatabase(onlyLoadFlag))
		return false;

	if (ibConfigurationTree* const tree = GetMetaTree()) {
		if (!tree->Load(m_metaData))
			return false;
	}

	// ⚠ NO METAOBJECT, as the desktop's file document has none: an editor's metaobject is what a configuration's Closed
	// closes (ibMetaTreeBase::CloseDocuments), and this one closes its own configuration — it would close itself again.
	if (ibValueMetaObject* const root = m_metaData->GetCommonMetaObject()) {
		if (GetTitle().IsEmpty())
			SetTitle(root->GetModuleName());
	}
	return true;
}

bool ibMetadataFileDocument::DoSaveDocument(const wxString& /*filename*/)
{
	// Read-only browser — nothing to write.
	return true;
}
