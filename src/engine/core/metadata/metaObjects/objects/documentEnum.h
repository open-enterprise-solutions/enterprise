#ifndef _DOCUMENT_ENUM_H_
#define _DOCUMENT_ENUM_H_

enum eDocumentWriteMode {
	ePosting,
	eUndoPosting,
	eWrite
};

enum eDocumentPostingMode {
	eRealTime,
	eRegular
};

#include "compiler/enumObject.h"

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

	void CreateEnumeration()
	{
		AddEnumeration(ePosting, wxT("posting"));
		AddEnumeration(eUndoPosting, wxT("undoPosting"));
		AddEnumeration(eWrite, wxT("write"));
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

	void CreateEnumeration()
	{
		AddEnumeration(eRealTime, wxT("realTime"));
		AddEnumeration(eRegular, wxT("regular"));
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
