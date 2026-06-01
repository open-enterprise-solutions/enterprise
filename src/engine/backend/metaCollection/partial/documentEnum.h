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
#pragma endregion 

#endif // ! _DOCUMENT_EMUN_H_
