#include "metaData.h"

#include "frmclient/backend/compiler/value.h"   // ibValue::GetAvailableCtor — the classes the server named
#include "frmclient/docView/tabDocument.h"   // ibTabDocument::Fetch — a question to the tab
#include "frmclient/mainFrame/mainFrame.h"   // ibFrontendMainFrame::FindTab — the tab as the frame was last answered
#include "protocol/protocol.h"

ibCtorAbstractType* ibMetaData::GetAvailableCtor(const ibClassID& clsid) const
{
	return ibValue::GetAvailableCtor(clsid);
}

bool ibMetaData::IsEditable() const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	return frame == nullptr || m_tabId == 0 || !frame->FindTab(m_tabId).GetBool(ibProtocol::ibFrame::ibTab::ReadOnly);
}

bool ibMetaData::Fetch(const ibProtocolNode& request, ibProtocolNode& answer) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	const ibTabDocument* const document = frame != nullptr && m_tabId != 0 ? frame->FindDocument(m_tabId) : nullptr;
	return document != nullptr && document->Fetch(request, answer);
}
