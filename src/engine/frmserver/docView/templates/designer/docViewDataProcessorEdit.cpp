#include "docViewDataProcessorEdit.h"

#include "core/serialize/dataBuilder.h"

#include "backend/backend_picture.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibDataProcessorTreeView, ibMetaView);

bool ibDataProcessorTreeView::OnCreate(ibDocument* doc, long flags)
{
	// Drawn for the client whose tab it is.
	m_metaTree = std::make_unique<ibDataProcessorTree>(GetDocChildFrame() != nullptr ? GetDocChildFrame()->GetFrame() : nullptr,
		GetDocument());

	return ibView::OnCreate(doc, flags);
}

void ibDataProcessorTreeView::OnDraw(ibDataNode& frame)
{
	m_metaTree->OnDraw(frame.Child(wxT("MetaData")));
}

wxIMPLEMENT_DYNAMIC_CLASS(ibDataProcessorEditDocument, ibMetaDataDocument);

// A new one named as the desktop named it — its class and a number.
static int s_defaultDataProcessorNameCounter = 1;

ibPictureDescription ibDataProcessorEditDocument::GetIcon() const
{
	if (m_metaData != nullptr) {
		ibValueMetaObject* metaObject = m_metaData->GetCommonMetaObject();
		wxASSERT(metaObject);
		return ibPictureDescription(metaObject->GetClassType());
	}
	return ibMetaDataDocument::GetIcon();
}

bool ibDataProcessorEditDocument::OnCreate(const wxString& path, long flags)
{
	m_metaData = ibMetaData::MakeShared<ibMetaDataDataProcessor>();
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

bool ibDataProcessorEditDocument::OnNewDocument()
{
	ibValueMetaObject* commonObject = m_metaData->GetCommonMetaObject();
	wxASSERT(commonObject);

	// notice that there is no need to neither reset nor even check the
	// modified flag here as the document itself is a new object (this is only
	// called from CreateDocument()) and so it shouldn't be saved anyhow even
	// if it is modified -- this could happen if the user code creates
	// documents pre-filled with some user-entered (and which hence must not be
	// lost) information

	SetDocumentSaved(false);

	const wxString name =
		wxString::Format(commonObject->GetClassName() + wxT("%d"), s_defaultDataProcessorNameCounter++);

	SetTitle(name);
	SetFilename(name, true);

	commonObject->SetName(name);

	m_metaData->Modify(true);

	if (!m_metaData->RunDatabase())
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return true;
}

bool ibDataProcessorEditDocument::OnCloseDocument()
{
	if (!m_metaData->CloseDatabase(forceCloseFlag)) {
		return false;
	}
	return ibMetaDocument::OnCloseDocument();
}

bool ibDataProcessorEditDocument::DoOpenDocument(const wxString& filename)
{
	if (!m_metaData->LoadFromTempFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return true;
}

bool ibDataProcessorEditDocument::DoSaveDocument(const wxString& filename)
{
	// The desktop tree's Save — the metadata written, when there was something to write; nothing to write is no
	// failure.
	if (m_metaData->IsModified())
		m_metaData->SaveDatabase();

	if (!m_metaData->SaveToTempFile(filename))
		return false;

	return true;
}

// ⭐ ITS METADATA'S MARK, NOT A SECOND ONE — what the desktop's tree copied into its document on every signal is read
// where it lives: the tab's "*" (ibClientChildFrame::GetTitle), a save, a close's question.
bool ibDataProcessorEditDocument::IsModified() const
{
	return m_metaData != nullptr && m_metaData->IsModified();
}

void ibDataProcessorEditDocument::Modify(bool modified)
{
	if (m_metaData != nullptr)
		m_metaData->Modify(modified);
}

ibDataProcessorTree* ibDataProcessorEditDocument::GetMetaTree() const
{
	// GUARD THE CAST, not the pointer that went into it: a first view of another kind makes
	// wxDynamicCast yield null, and the arrow was applied to it regardless.
	ibDataProcessorTreeView* view = wxDynamicCast(GetFirstView(), ibDataProcessorTreeView);
	return view != nullptr ? view->GetMetaTree() : nullptr;
}
