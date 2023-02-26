#ifndef _DOCUMENT_ENUM_H_
#define _DOCUMENT_ENUM_H_

enum eDocumentWriteMode {
	eDocumentWriteMode_Posting,
	eDocumentWriteMode_UndoPosting,
	eDocumentWriteMode_Write
};

enum eDocumentPostingMode {
	eDocumentPostingMode_RealTime,
	eDocumentPostingMode_Regular
};

#include "core/compiler/enumObject.h"

class CValueEnumDocumentWriteMode : public IEnumeration<eDocumentWriteMode> {
	wxDECLARE_DYNAMIC_CLASS(CValueEnumDocumentWriteMode);
public:

	CValueEnumDocumentWriteMode() : IEnumeration() {
		InitializeEnumeration();
	}

	CValueEnumDocumentWriteMode(eDocumentWriteMode writeMode) : IEnumeration(writeMode) {
		InitializeEnumeration(writeMode);
	}

	virtual wxString GetTypeString() const override {
		return wxT("documentWriteMode");
	}

protected:

	void CreateEnumeration() {
		AddEnumeration(eDocumentWriteMode_Posting, wxT("posting"));
		AddEnumeration(eDocumentWriteMode_UndoPosting, wxT("undoPosting"));
		AddEnumeration(eDocumentWriteMode_Write, wxT("write"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eDocumentWriteMode value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

class CValueEnumDocumentPostingMode : public IEnumeration<eDocumentPostingMode> {
	wxDECLARE_DYNAMIC_CLASS(CValueEnumDocumentPostingMode);
public:

	CValueEnumDocumentPostingMode() : IEnumeration() {
		InitializeEnumeration();
	}

	CValueEnumDocumentPostingMode(eDocumentPostingMode postingMode) : IEnumeration(postingMode) {
		InitializeEnumeration(postingMode);
	}

	virtual wxString GetTypeString() const override {
		return wxT("documentPostingMode");
	}

protected:

	void CreateEnumeration() {
		AddEnumeration(eDocumentPostingMode_RealTime, wxT("realTime"));
		AddEnumeration(eDocumentPostingMode_Regular, wxT("regular"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(eDocumentPostingMode value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

#endif // ! _DOCUMENT_EMUN_H_
