#ifndef _VALUEORIENT_H__
#define _VALUEORIENT_H__

#include "compiler/enumObject.h"

class CValueEnumOrient : public IEnumeration<wxOrientation> {
	wxDECLARE_DYNAMIC_CLASS(CValueEnumOrient);
public:

	CValueEnumOrient() : IEnumeration() { InitializeEnumeration(); }
	CValueEnumOrient(wxOrientation orient) : IEnumeration(orient) { InitializeEnumeration(orient); }

	wxString GetTypeString() const override { return wxT("orient"); }

protected:

	void CreateEnumeration()
	{
		AddEnumeration(wxHORIZONTAL, wxT("horizontal"));
		AddEnumeration(wxVERTICAL, wxT("vertical"));
	}

	virtual void InitializeEnumeration() override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration();
	}

	virtual void InitializeEnumeration(wxOrientation value) override
	{
		CreateEnumeration();
		IEnumeration::InitializeEnumeration(value);
	}
};

#endif