#include "docViewDataReportEdit.h"

#include "core/serialize/dataBuilder.h"

#include "backend/backend_picture.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibReportTreeView, ibMetaView);

bool ibReportTreeView::OnCreate(ibDocument* doc, long flags)
{
	// Drawn for the client whose tab it is.
	m_metaTree = std::make_unique<ibDataReportTree>(GetDocChildFrame() != nullptr ? GetDocChildFrame()->GetFrame() : nullptr,
		GetDocument());

	return ibView::OnCreate(doc, flags);
}

void ibReportTreeView::OnDraw(ibDataNode& frame)
{
	m_metaTree->OnDraw(frame.Child(ibProtocol::ibView::MetaData));
}

wxIMPLEMENT_DYNAMIC_CLASS(ibReportEditDocument, ibMetaDataDocument);

// A new one named as the desktop named it — its class and a number.
static int s_defaultReportNameCounter = 1;

ibPictureDescription ibReportEditDocument::GetIcon() const
{
	if (m_metaData != nullptr) {
		ibValueMetaObject* metaObject = m_metaData->GetCommonMetaObject();
		wxASSERT(metaObject);
		return ibPictureDescription(metaObject->GetClassType());
	}
	return ibMetaDataDocument::GetIcon();
}

bool ibReportEditDocument::OnCreate(const ibString& path, long flags)
{
	m_metaData = ibMetaData::MakeShared<ibMetaDataReport>();
	if (!ibMetaDocument::OnCreate(path, flags))
		return false;
	return true;
}

bool ibReportEditDocument::OnNewDocument()
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

	const ibString name =
		ibString::Format(commonObject->GetClassName() + ibT("%d"), s_defaultReportNameCounter++);

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

bool ibReportEditDocument::OnCloseDocument()
{
	if (!m_metaData->CloseDatabase(forceCloseFlag)) {
		return false;
	}
	return ibMetaDocument::OnCloseDocument();
}

bool ibReportEditDocument::DoOpenDocument(const ibString& filename)
{
	if (!m_metaData->LoadFromTempFile(filename))
		return false;

	if (!GetMetaTree()->Load(m_metaData))
		return false;

	return true;
}

bool ibReportEditDocument::DoSaveDocument(const ibString& filename)
{
	// The desktop tree's Save — the metadata written, when there was something to write; nothing to write is no
	// failure. ⚠ The desktop landed its open composer editors first (ibCommitOpenComposers): the server has none yet.
	if (m_metaData->IsModified())
		m_metaData->SaveDatabase();

	if (!m_metaData->SaveToTempFile(filename))
		return false;

	return true;
}

// ⭐ ITS METADATA'S MARK, NOT A SECOND ONE — see ibDataProcessorEditDocument::IsModified.
bool ibReportEditDocument::IsModified() const
{
	return m_metaData != nullptr && m_metaData->IsModified();
}

void ibReportEditDocument::Modify(bool modified)
{
	if (m_metaData != nullptr)
		m_metaData->Modify(modified);
}

ibDataReportTree* ibReportEditDocument::GetMetaTree() const
{
	// GUARD THE CAST, not the pointer that went into it — see the twin in docViewDataProcessorEdit.cpp.
	ibReportTreeView* view = wxDynamicCast(GetFirstView(), ibReportTreeView);
	return view != nullptr ? view->GetMetaTree() : nullptr;
}
