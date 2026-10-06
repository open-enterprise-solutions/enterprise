////////////////////////////////////////////////////////////////////////////
//	Description : Registration journal — doc/view tab
////////////////////////////////////////////////////////////////////////////

#include "docViewAuditLog.h"

#include "backend/appData.h"
#include "backend/logger/logger.h"

// ============================================================
//   ibAuditLogDocument
// ============================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibAuditLogDocument, ibDocument);

ibAuditLogDocument::ibAuditLogDocument() : ibDocument()
{
	// ibDocument default ctor already leaves m_documentParent null, so
	// IsChildDocument() returns false — no flag to set.

	if (appData != nullptr && appData->GetLogger() != nullptr) {
		m_reader = std::make_unique<ibLoggerReader>(
			appData->GetLogger()->GetLogDir());
	}
}

// ============================================================
//   ibAuditLogView
// ============================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibAuditLogView, ibView);

void ibAuditLogView::OnDraw(ibDataNode& WXUNUSED(frame))
{
}
