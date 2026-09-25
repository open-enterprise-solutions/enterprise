#include "valueSecureConnection.h"

#include "backend/backend_exception.h"

#include <wx/filefn.h>

namespace {

// The object a value stands for, if it is a T - through GetRef(), which answers the value itself when it refers
// to nothing (the holder's own constructor reads a union a string shares; see valueHttp.cpp).
template <class T>
T* ibSecureHeld(const ibValue& value)
{
	return dynamic_cast<T*>(value.GetRef());
}

// A CertificateFile, or nothing said (Undefined), or a refusal by the name of the property.
ibValueCertificateFile* ibSecureCertificateOrNone(const ibValue& value, const wxChar* what)
{
	if (value.GetType() == ibValueTypes::TYPE_EMPTY)
		return nullptr;
	ibValueCertificateFile* const file = ibSecureHeld<ibValueCertificateFile>(value);
	if (file == nullptr)
		ibBackendCoreException::Error(_("SecureConnection: %s is a CertificateFile, or Undefined"), wxString(what));
	return file;
}

} // namespace

//////////////////////////////////////////////////////////////////////
// CertificateFile
//////////////////////////////////////////////////////////////////////

void ibValueCertificateFile_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("CertificateFile(path : string)"));
	helper.AppendConstructor(2, wxT("CertificateFile(path : string, password : string)"));

	helper.AppendProp(wxT("Path"), true, false, wxNOT_FOUND);
}

ibValueCertificateFile::ibValueCertificateFile()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true)
{
}

ibValueCertificateFile::~ibValueCertificateFile() {}

bool ibValueCertificateFile::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;
	Open(paParams[0]->GetString(), lSizeArray > 1 ? paParams[1]->GetString() : wxString());
	return true;
}

void ibValueCertificateFile::Open(const wxString& path, const wxString& password)
{
	// The file has to be there NOW - a wrong path is found at the desk, not at the bank's door. What is in it
	// is read by the library, when a connection is made, and refused there by this name.
	if (path.empty() || !wxFileExists(path))
		ibBackendCoreException::Error(_("CertificateFile: there is no file '%s'"), path);
	m_path = path;
	m_password = password;
}

bool ibValueCertificateFile::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (lPropNum != enPath)
		return false;
	pvarPropVal = m_path;
	return true;
}

//////////////////////////////////////////////////////////////////////
// SecureConnection
//////////////////////////////////////////////////////////////////////

void ibValueSecureConnection_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, wxT("SecureConnection()"));
	helper.AppendConstructor(1, wxT("SecureConnection(clientCertificate : CertificateFile)"));
	helper.AppendConstructor(2, wxT("SecureConnection(clientCertificate : CertificateFile, trustedCertificates : CertificateFile)"));

	helper.AppendProp(wxT("ClientCertificate"), true, true, wxNOT_FOUND);
	helper.AppendProp(wxT("TrustedCertificates"), true, true, wxNOT_FOUND);
	helper.AppendProp(wxT("VerifyServerCertificate"), true, true, wxNOT_FOUND);
}

ibValueSecureConnection::ibValueSecureConnection()
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true)
{
}

ibValueSecureConnection::~ibValueSecureConnection() {}

bool ibValueSecureConnection::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray > 0)
		SetClientCertificate(*paParams[0]);
	if (lSizeArray > 1)
		SetTrustedCertificates(*paParams[1]);
	return true;
}

void ibValueSecureConnection::SetClientCertificate(const ibValue& value)
{
	m_client = ibSecureCertificateOrNone(value, wxT("ClientCertificate"));
}

void ibValueSecureConnection::SetTrustedCertificates(const ibValue& value)
{
	m_trusted = ibSecureCertificateOrNone(value, wxT("TrustedCertificates"));
}

bool ibValueSecureConnection::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enClientCertificate:
		pvarPropVal = m_client;
		return true;
	case enTrustedCertificates:
		pvarPropVal = m_trusted;
		return true;
	case enVerifyServerCertificate:
		pvarPropVal = m_verify;
		return true;
	}
	return false;
}

bool ibValueSecureConnection::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum) {
	case enClientCertificate:
		SetClientCertificate(varPropVal);
		return true;
	case enTrustedCertificates:
		SetTrustedCertificates(varPropVal);
		return true;
	case enVerifyServerCertificate:
		// A Boolean and nothing else: "yes" or 1 read as true would switch a check off by a typo.
		if (varPropVal.GetType() != ibValueTypes::TYPE_BOOLEAN)
			ibBackendCoreException::Error(_("SecureConnection: VerifyServerCertificate is True or False"));
		m_verify = varPropVal.GetBoolean();
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueCertificateFile, "CertificateFile", value_to_clsid("VL_CRTF"));
VALUE_TYPE_REGISTER(ibValueSecureConnection, "SecureConnection", value_to_clsid("VL_SECN"));
