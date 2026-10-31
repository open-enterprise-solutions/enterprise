#include "tabDocument.h"

#include <wx/filedlg.h>   // wxFileSelector — Save as, where the person keeps the file

#include "frmclient/mainFrame/mainFrame.h"

ibTabDocument::ibTabDocument(long long tabId)
	: m_tabId(tabId)
{
	// Its data is the server's, kept where the server keeps it: Save goes there, never to a file chosen here.
	SetDocumentSaved(true);
}

ibTabDocument::~ibTabDocument()
{
	if (ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame())
		frame->RemoveDocument(this);
}

wxString ibTabDocument::GetUniqueIdentifier() const
{
	return wxString::Format(wxT("%lld"), m_tabId);
}

bool ibTabDocument::OnCloseDocument()
{
	ibFrontendDocManager* documentManager = GetDocumentManager();

	// When the parent document closes, its children must be closed as well as they can't exist without the parent.
	ibFrontendDocument const* documentParent = m_documentParent;

	if (documentManager != nullptr && documentParent != nullptr)
		documentManager->ActivateView(documentParent->GetFirstView());

	return ibFrontendDocument::OnCloseDocument();
}

bool ibTabDocument::IsCloseOnOwnerClose() const
{
	const ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame == nullptr || frame->FindTab(m_tabId).GetId() != m_tabId;
}

bool ibTabDocument::IsModified() const
{
	return IsCommandEnabled(ibProtocol::ibFrame::ibCommand::Save);
}

bool ibTabDocument::Close()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame != nullptr && frame->FindTab(m_tabId).GetId() == m_tabId) {
		frame->CloseTab(m_tabId);
		return false;
	}
	return ibFrontendDocument::Close();
}

bool ibTabDocument::Save()
{
	if (GetSaveFilter().IsEmpty()) {
		DoCommand(ibProtocol::ibFrame::ibCommand::Save);
		return true;
	}
	if (GetFilename().IsEmpty())
		return SaveAs();
	return OnSaveDocument(GetFilename());
}

bool ibTabDocument::SaveAs()
{
	const wxString filter = GetSaveFilter();
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (filter.IsEmpty() || frame == nullptr)
		return false;

	// Named as its tab is, the first time — a modified one's title ends with its «*».
	wxString name = GetFilename().IsEmpty() ? frame->FindTab(m_tabId).GetString(ibProtocol::ibFrame::ibTab::Title).ToWxString()
		: wxFileNameFromPath(GetFilename());
	name.Trim();
	if (name.EndsWith(wxT("*")))
		name.RemoveLast();

	const wxString path = wxFileSelector(_("Save As"), wxPathOnly(GetFilename()), name, wxEmptyString, filter,
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT, frame);
	return !path.IsEmpty() && OnSaveDocument(path);
}

wxString ibTabDocument::GetSaveFilter() const
{
	const ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return wxString();

	wxString filter;
	for (const ibProtocolNode& format : frame->FindTab(m_tabId).FindChild(ibProtocol::ibFrame::ibTab::Formats).Children()) {
		if (!filter.IsEmpty())
			filter += wxT("|");
		filter += format.GetString(ibProtocol::ibFrame::ibTab::Title) + wxT("|") + format.GetString(ibProtocol::ibFrame::ibTab::Mask);
	}
	return filter;
}

bool ibTabDocument::DoSaveDocument(const wxString& file)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	// SAVED BY THE SERVER as a file of this one's name — written as the name says — and taken down from it, the tab's
	// File: the document is the server's, the file on the disk the person's.
	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibFrame::Command, static_cast<long long>(ibProtocol::ibFrame::ibCommand::SaveAs))
		.SetValue(ibProtocol::ibFrame::Name, wxFileNameFromPath(file));
	if (!frame->Call(ibProtocol::ibMethod::Command, params, answer))
		return false;
	const wxString saved = frame->FindTab(m_tabId).GetString(ibProtocol::ibFrame::ibTab::File);
	return !saved.IsEmpty() && frame->DownloadFile(saved, file);
}

bool ibTabDocument::IsCommandEnabled(ibProtocol::ibFrame::ibCommand command) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	for (const ibProtocolNode& taken : frame->FindTab(m_tabId).GetList(ibProtocol::ibFrame::ibTab::Commands)) {
		if (taken.AsInt() == static_cast<long long>(command))
			return true;
	}
	return false;
}

void ibTabDocument::DoCommand(ibProtocol::ibFrame::ibCommand command) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return;

	// Posted, as the person's acts on a tab are: it goes after the tab was made the server's active one, which the
	// command is done on.
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibFrame::Command, static_cast<long long>(command));
	frame->Post(ibProtocol::ibMethod::Command, params);
}

bool ibTabDocument::Fetch(const ibProtocolNode& request, ibProtocolNode& answer) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibFetch::Tab, m_tabId)
		.SetValue(ibProtocol::ibFetch::Request, request);
	return frame->Fetch(ibProtocol::ibMethod::Fetch, params, answer);
}

void ibTabDocument::Post(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return;

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibEvent::Tab, m_tabId)
		.SetValue(ibProtocol::ibEvent::Event, static_cast<int>(event))
		.SetValue(ibProtocol::ibEvent::Args, args);
	frame->Post(ibProtocol::ibMethod::Event, params);
}

void ibTabDocument::Hold(ibProtocol::ibEvent::ibKind event, const ibProtocolNode& args) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return;

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibEvent::Tab, m_tabId)
		.SetValue(ibProtocol::ibEvent::Event, static_cast<int>(event))
		.SetValue(ibProtocol::ibEvent::Args, args);
	if (!frame->Hold(params))
		frame->Post(ibProtocol::ibMethod::Event, params);
}
