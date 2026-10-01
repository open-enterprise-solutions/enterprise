#ifndef __DOCUMENT_ENUM_H__
#define __DOCUMENT_ENUM_H__

enum ibDocumentWriteMode {
	ibDocumentWriteMode_Posting,
	ibDocumentWriteMode_UndoPosting,
	ibDocumentWriteMode_Write
};

enum ibDocumentPostingMode {
	ibDocumentPostingMode_RealTime,
	ibDocumentPostingMode_Regular
};

// WHAT THE PLATFORM DOES WITH A DOCUMENT'S MOVEMENTS when it is posted again or its posting undone (Max, 2026-09-14).
// A deleted document takes them with it whatever this says (Max, 2026-09-15). The numbers are what a saved
// configuration holds.
enum ibDocumentRecordsDeletion {
	ibDocumentRecordsDeletion_Automatically,    // cleared before a posting again and when a posting is undone
	ibDocumentRecordsDeletion_OnUndoPosting,    // kept when posted again — a set the handler writes replaces its own —
	                                            // and cleared when a posting is undone
	ibDocumentRecordsDeletion_Never             // not cleared by the platform on either: the configuration clears them
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"

class ibValueEnumDocumentWriteMode : public ibValueEnumeration<ibDocumentWriteMode> {
	public:
	ibValueEnumDocumentWriteMode() : ibValueEnumeration() {}
	//ibValueEnumDocumentWriteMode(ibDocumentWriteMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_Posting, wxT("Posting"), _("Posting"));
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_UndoPosting, wxT("UndoPosting"), _("Undo posting"));
		AddEnumeration(ibDocumentWriteMode::ibDocumentWriteMode_Write, wxT("Write"), _("Write"));
	}
};
class ibValueEnumDocumentPostingMode : public ibValueEnumeration<ibDocumentPostingMode> {
	public:
	ibValueEnumDocumentPostingMode() : ibValueEnumeration() {}
	//ibValueEnumDocumentPostingMode(ibDocumentPostingMode mode) : ibValueEnumeration(mode) {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDocumentPostingMode::ibDocumentPostingMode_RealTime, wxT("RealTime"), _("Real time"));
		AddEnumeration(ibDocumentPostingMode::ibDocumentPostingMode_Regular, wxT("Regular"), _("Regular"));
	}
};
class ibValueEnumDocumentRecordsDeletion : public ibValueEnumeration<ibDocumentRecordsDeletion> {
	public:
	ibValueEnumDocumentRecordsDeletion() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibDocumentRecordsDeletion::ibDocumentRecordsDeletion_Automatically, wxT("Automatically"), _("Delete automatically"));
		AddEnumeration(ibDocumentRecordsDeletion::ibDocumentRecordsDeletion_OnUndoPosting, wxT("OnUndoPosting"), _("Delete automatically on undo posting"));
		AddEnumeration(ibDocumentRecordsDeletion::ibDocumentRecordsDeletion_Never, wxT("Never"), _("Do not delete automatically"));
	}
};
#pragma endregion 

#endif // ! _DOCUMENT_EMUN_H_
