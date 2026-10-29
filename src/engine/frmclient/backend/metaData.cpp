#include "metaData.h"

#include "frmclient/backend/compiler/value.h"   // ibValue::GetAvailableCtor — the classes the server named
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
