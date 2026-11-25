#include "valueMail.h"

#include "backend/backend_exception.h"
#include "backend/system/value/valueArray.h"        // ibValueArray - To, Cc, Bcc
#include "backend/system/value/valueBinaryData.h"   // ibValueBinaryData - an attachment's bytes; a file read whole

#include "core/fileSystem.h"           // ibPathOf - an attached file's name
#include "core/net/smtpClient.h"       // the transport - the core's, with its TLS

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace {

const int kMaxTimeout = 24 * 60 * 60;   // a day, as HTTPConnection's

// Whether an argument was given - asked of its type: a zero is an empty number, not a gap.
bool Given(ibValue** paParams, const long lSizeArray, const long at)
{
	return lSizeArray > at && paParams[at]->GetType() != ibValueTypes::TYPE_EMPTY;
}

std::string Utf8(const ibString& text)
{
	return text.ToUtf8();
}

// A file's bytes as the core takes them - read through the door BinaryData reads by, refused with the file named.
std::string FileBytes(const ibString& path, const ibString& who)
{
	ibMemoryBuffer bytes;
	ibValueBinaryData::ReadWholeFile(path, who, bytes);
	return std::string(static_cast<const char*>(bytes.GetData()), bytes.GetDataLen());
}

// An address list - an Array of strings - in the core's words.
void AddressesOf(const ibValue& list, std::vector<std::string>& addresses)
{
	const ibValueArray* const array = dynamic_cast<ibValueArray*>(list.GetRef());
	if (array == nullptr)
		return;
	for (const ibValue& one : array->Values()) {
		if (one.GetType() != ibValueTypes::TYPE_STRING)
			ibBackendCoreException::Error(_("InternetMailMessage: an address is a string - 'address' or 'Name <address>': '%s' is not one"), one.GetString());
		addresses.push_back(Utf8(one.GetString()));
	}
}

// WHY THE SERVER WAS NOT SPOKEN TO, in a sentence: the transport's own words where HTTP's are the same.
ibString Why(ibSmtpError error, const std::string& detail, int timeout)
{
	const ibString said = ibString::FromUTF8(detail);
	switch (error) {
	case ibSmtpError::Timeout:
		return ibString::Format(_("nobody answered within the %d s allowed"), timeout);
	case ibSmtpError::Connection:
		return _("the connection could not be made - the name does not resolve, or nothing listens on the port");
	case ibSmtpError::Verification:
		return ibString::Format(_("the server's certificate is not trusted - not signed by a trusted authority, expired, or not a certificate (%s); name the authority's file in TrustedCertificates"), said);
	case ibSmtpError::Hostname:
		return _("the server's certificate is not for this host name");
	case ibSmtpError::Handshake:
		return _("the server on this port does not speak TLS, or the handshake failed");
	case ibSmtpError::NoStartTls:
		return _("the server does not offer STARTTLS, so the connection cannot be made secure on this port - TLS from the first byte is port 465");
	case ibSmtpError::NoLogin:
		return ibString::Format(_("the server offers no login this client speaks (PLAIN or LOGIN) - %s"), said);
	case ibSmtpError::Refused:
		return ibString::Format(_("the server refused - %s"), said);
	case ibSmtpError::Message:
		return ibString::Format(_("the message cannot be sent - %s"), said);
	case ibSmtpError::Lost:
		return ibString::Format(_("the connection was lost - %s"), said);
	case ibSmtpError::Answer:
		return ibString::Format(_("what came back is not SMTP - %s"), said);
	default:
		return said;
	}
}

} // namespace

//*********************************************************************************************************************
//*                                               InternetMailProfile                                                 *
//*********************************************************************************************************************

void ibValueInternetMailProfile_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, ibT("InternetMailProfile()"));

	helper.AppendProp(ibT("SMTPServerAddress"), true, true, ibNOT_FOUND);
	helper.AppendProp(ibT("SMTPPort"), true, true, ibNOT_FOUND);
	helper.AppendProp(ibT("SMTPUser"), true, true, ibNOT_FOUND);
	helper.AppendProp(ibT("SMTPPassword"), false, true, ibNOT_FOUND);
	helper.AppendProp(ibT("Timeout"), true, true, ibNOT_FOUND);
	helper.AppendProp(ibT("SecureConnection"), true, true, ibNOT_FOUND);
}

ibValueInternetMailProfile::ibValueInternetMailProfile() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueInternetMailProfile::~ibValueInternetMailProfile()
{
}

int ibValueInternetMailProfile::GetPort() const
{
	if (m_port != 0)
		return m_port;
	return m_secure != nullptr ? 465 : 25;
}

bool ibValueInternetMailProfile::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enSMTPServerAddress:
		pvarPropVal = m_server;
		return true;
	case enSMTPPort:
		pvarPropVal = GetPort();
		return true;
	case enSMTPUser:
		pvarPropVal = m_user;
		return true;
	case enTimeout:
		pvarPropVal = m_timeout;
		return true;
	case enSecureConnection:
		if (m_secure != nullptr)
			pvarPropVal = m_secure;
		else
			pvarPropVal = ibValue();
		return true;
	}
	return false;
}

bool ibValueInternetMailProfile::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum) {
	case enSMTPServerAddress:
		m_server = varPropVal.GetString();
		return true;
	case enSMTPPort: {
		const long long port = varPropVal.GetInteger();
		if (port < 0 || port > 65535)
			ibBackendCoreException::Error(_("InternetMailProfile: a port is a number from 1 to 65535, or 0 for the one the security says: %d was given"), static_cast<int>(port));
		m_port = static_cast<int>(port);
		return true;
	}
	case enSMTPUser:
		m_user = varPropVal.GetString();
		return true;
	case enSMTPPassword:
		m_password = varPropVal.GetString();
		return true;
	case enTimeout: {
		const long long seconds = varPropVal.GetInteger();
		if (seconds < 1)
			ibBackendCoreException::Error(_("InternetMailProfile: the time allowed is a number of seconds, 1 or more - never 'forever': %d was given"), static_cast<int>(seconds));
		if (seconds > kMaxTimeout)
			ibBackendCoreException::Error(_("InternetMailProfile: the time allowed is at most %d s, a day: %d was given"), kMaxTimeout, static_cast<int>(seconds));
		m_timeout = static_cast<int>(seconds);
		return true;
	}
	case enSecureConnection:
		if (varPropVal.GetType() == ibValueTypes::TYPE_EMPTY) {
			m_secure = nullptr;
			return true;
		}
		if (ibValueSecureConnection* const secure = dynamic_cast<ibValueSecureConnection*>(varPropVal.GetRef())) {
			m_secure = secure;
			return true;
		}
		ibBackendCoreException::Error(_("InternetMailProfile: SecureConnection is a SecureConnection value, or Undefined for a connection in the clear"));
	}
	return false;
}

//*********************************************************************************************************************
//*                                               InternetMailText(s)                                                 *
//*********************************************************************************************************************

void ibValueInternetMailText_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("Text"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("TextType"), true, false, ibNOT_FOUND);
}

ibValueInternetMailText::ibValueInternetMailText() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueInternetMailText::ibValueInternetMailText(const ibString& text, ibInternetMailTextType type)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_text(text), m_type(type)
{
}

ibValueInternetMailText::~ibValueInternetMailText()
{
}

bool ibValueInternetMailText::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enText:
		pvarPropVal = m_text;
		return true;
	case enTextType:
		pvarPropVal = ibValue::CreateEnumObject<ibValueEnumInternetMailTextType>(m_type);
		return true;
	}
	return false;
}

void ibValueInternetMailTexts_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	ibValueInternetMailTexts::FillCollection(helper);
	helper.AppendFunc(ibT("Add"), 2, ibT("Add(text : string, textType : InternetMailTextType = PlainText) : InternetMailText"));
}

ibValueInternetMailTexts::ibValueInternetMailTexts() : ibValueCollection(ibValueTypes::TYPE_VALUE)
{
}

bool ibValueInternetMailTexts::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum != enAdd)
		return ibValueCollection::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	if (lSizeArray < 1)
		return false;
	const ibInternetMailTextType type = Given(paParams, lSizeArray, 1)
		? paParams[1]->ConvertToEnumValue<ibInternetMailTextType>() : ibInternetMailTextType_PlainText;
	const ibValuePtr<ibValueInternetMailText> text(new ibValueInternetMailText(paParams[0]->GetString(), type));
	m_listValue.push_back(text);
	pvarRetValue = text;
	return true;
}

ibValue ibValueInternetMailTexts::ElementValue(const ibValuePtr<ibValueInternetMailText>& text) const
{
	if (text == nullptr)
		return ibValue(new ibValueInternetMailText());
	return text;
}

//*********************************************************************************************************************
//*                                            InternetMailAttachment(s)                                              *
//*********************************************************************************************************************

void ibValueInternetMailAttachment_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("Name"), true, true, ibNOT_FOUND);
	helper.AppendProp(ibT("Data"), true, false, ibNOT_FOUND);
}

ibValueInternetMailAttachment::ibValueInternetMailAttachment() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

ibValueInternetMailAttachment::ibValueInternetMailAttachment(const ibString& name, const ibMemoryBuffer& bytes)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE), m_name(name), m_bytes(bytes)
{
}

ibValueInternetMailAttachment::~ibValueInternetMailAttachment()
{
}

bool ibValueInternetMailAttachment::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enName:
		pvarPropVal = m_name;
		return true;
	case enData:
		pvarPropVal = ibValuePtr<ibValueBinaryData>(new ibValueBinaryData(m_bytes));
		return true;
	}
	return false;
}

bool ibValueInternetMailAttachment::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum) {
	case enName:
		m_name = varPropVal.GetString();
		return true;
	}
	return false;
}

void ibValueInternetMailAttachments_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	ibValueInternetMailAttachments::FillCollection(helper);
	helper.AppendFunc(ibT("Add"), 2, ibT("Add(source : BinaryData | string, name : string = \"\") : InternetMailAttachment"));
}

ibValueInternetMailAttachments::ibValueInternetMailAttachments() : ibValueCollection(ibValueTypes::TYPE_VALUE)
{
}

// BYTES OR A FILE — BinaryData as it is, a file read now, whole: what the message carries is what was there when it
// was added. Named as given; a file without a name given goes by its own, bytes without one as "attachment".
bool ibValueInternetMailAttachments::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum != enAdd)
		return ibValueCollection::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
	if (lSizeArray < 1)
		return false;

	ibString name = Given(paParams, lSizeArray, 1) ? paParams[1]->GetString() : ibString();
	ibMemoryBuffer bytes;
	if (const ibValueBinaryData* const data = dynamic_cast<ibValueBinaryData*>(paParams[0]->GetRef())) {
		bytes = data->GetBuffer();
		if (name.IsEmpty())
			name = ibT("attachment");
	}
	else if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		const ibString path = paParams[0]->GetString();
		ibValueBinaryData::ReadWholeFile(path, ibT("InternetMailAttachments"), bytes);
		if (name.IsEmpty())
			name = ibTextOf(ibPathOf(path).filename());
	}
	else
		ibBackendCoreException::Error(_("InternetMailAttachments: an attachment is BinaryData or a file's name"));

	const ibValuePtr<ibValueInternetMailAttachment> attachment(new ibValueInternetMailAttachment(name, bytes));
	m_listValue.push_back(attachment);
	pvarRetValue = attachment;
	return true;
}

ibValue ibValueInternetMailAttachments::ElementValue(const ibValuePtr<ibValueInternetMailAttachment>& attachment) const
{
	if (attachment == nullptr)
		return ibValue(new ibValueInternetMailAttachment());
	return attachment;
}

//*********************************************************************************************************************
//*                                               InternetMailMessage                                                 *
//*********************************************************************************************************************

void ibValueInternetMailMessage_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, ibT("InternetMailMessage()"));

	helper.AppendProp(ibT("From"), true, true, ibNOT_FOUND);
	helper.AppendProp(ibT("To"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("Cc"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("Bcc"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("Subject"), true, true, ibNOT_FOUND);
	helper.AppendProp(ibT("Texts"), true, false, ibNOT_FOUND);
	helper.AppendProp(ibT("Attachments"), true, false, ibNOT_FOUND);
}

ibValueInternetMailMessage::ibValueInternetMailMessage() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE),
	m_to(ibValuePtr<ibValueArray>(new ibValueArray())), m_cc(ibValuePtr<ibValueArray>(new ibValueArray())),
	m_bcc(ibValuePtr<ibValueArray>(new ibValueArray())),
	m_texts(new ibValueInternetMailTexts()), m_attachments(new ibValueInternetMailAttachments())
{
}

ibValueInternetMailMessage::~ibValueInternetMailMessage()
{
}

bool ibValueInternetMailMessage::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enFrom:
		pvarPropVal = m_from;
		return true;
	case enTo:
		pvarPropVal = m_to;
		return true;
	case enCc:
		pvarPropVal = m_cc;
		return true;
	case enBcc:
		pvarPropVal = m_bcc;
		return true;
	case enSubject:
		pvarPropVal = m_subject;
		return true;
	case enTexts:
		pvarPropVal = m_texts;
		return true;
	case enAttachments:
		pvarPropVal = m_attachments;
		return true;
	}
	return false;
}

bool ibValueInternetMailMessage::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	switch (lPropNum) {
	case enFrom:
		m_from = varPropVal.GetString();
		return true;
	case enSubject:
		m_subject = varPropVal.GetString();
		return true;
	}
	return false;
}

void ibValueInternetMailMessage::Fill(ibMailMessage& message) const
{
	message.m_from = Utf8(m_from);
	AddressesOf(m_to, message.m_to);
	AddressesOf(m_cc, message.m_cc);
	AddressesOf(m_bcc, message.m_bcc);
	message.m_subject = Utf8(m_subject);

	// The texts of a kind joined in the order added - one plain text and one HTML go out, as alternatives.
	for (const ibValuePtr<ibValueInternetMailText>& text : m_texts->Values()) {
		std::string& into = text->GetTextType() == ibInternetMailTextType_HTML ? message.m_html : message.m_plain;
		if (!into.empty())
			into += "\r\n";
		into += Utf8(text->GetText());
	}
	for (const ibValuePtr<ibValueInternetMailAttachment>& attachment : m_attachments->Values()) {
		const ibMemoryBuffer& bytes = attachment->GetBytes();
		message.m_attachments.push_back({ Utf8(attachment->GetName()),
			std::string(static_cast<const char*>(bytes.GetData()), bytes.GetDataLen()) });
	}
}

//*********************************************************************************************************************
//*                                                   InternetMail                                                    *
//*********************************************************************************************************************

void ibValueInternetMail_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, ibT("InternetMail()"));

	helper.AppendProc(ibT("Logon"), 1, ibT("Logon(profile : InternetMailProfile)"));
	helper.AppendProc(ibT("Send"), 1, ibT("Send(message : InternetMailMessage)"));
	helper.AppendProc(ibT("Logoff"), ibT("Logoff()"));
}

ibValueInternetMail::ibValueInternetMail() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE)
{
}

// Let go while logged on - goodbye said, as Logoff says it.
ibValueInternetMail::~ibValueInternetMail()
{
	if (m_client)
		m_client->Logoff(std::chrono::seconds(m_timeout));
}

bool ibValueInternetMail::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enLogon: {
		if (lSizeArray < 1)
			return false;
		const ibValueInternetMailProfile* const profile = dynamic_cast<ibValueInternetMailProfile*>(paParams[0]->GetRef());
		if (profile == nullptr)
			ibBackendCoreException::Error(_("InternetMail: Logon takes an InternetMailProfile"));
		Logon(*profile);
		return true;
	}
	case enSend: {
		if (lSizeArray < 1)
			return false;
		const ibValueInternetMailMessage* const message = dynamic_cast<ibValueInternetMailMessage*>(paParams[0]->GetRef());
		if (message == nullptr)
			ibBackendCoreException::Error(_("InternetMail: Send takes an InternetMailMessage"));
		Send(*message);
		return true;
	}
	case enLogoff:
		Logoff();
		return true;
	}
	return false;
}

// THE PROFILE READ NOW: the server, the port, the security, who logs on. A connection already open is closed first -
// a second Logon is a new one.
void ibValueInternetMail::Logon(const ibValueInternetMailProfile& profile)
{
	Logoff();
	if (profile.GetServer().IsEmpty())
		ibBackendCoreException::Error(_("InternetMail: the profile names no server - SMTPServerAddress is empty"));

	const int port = profile.GetPort();
	m_server = ibString::Format(ibT("%s:%d"), profile.GetServer(), port);
	m_timeout = profile.GetTimeout();

	std::unique_ptr<ibSmtpClient> client(new ibSmtpClient(Utf8(profile.GetServer()), static_cast<std::uint16_t>(port)));
	if (const ibValueSecureConnection* const secure = profile.GetSecureConnection()) {
		// Whom to trust: the file named, else the platform's own store; the certificate shown and its key are one file.
		const ibValueCertificateFile* const trusted = secure->TrustedCertificates();
		const ibValueCertificateFile* const mine = secure->ClientCertificate();
		const std::string trustedPem = trusted != nullptr ? FileBytes(trusted->Path(), ibT("InternetMail")) : std::string();
		const std::string certificatePem = mine != nullptr ? FileBytes(mine->Path(), ibT("InternetMail")) : std::string();
		std::string error;
		switch (client->Secure(trustedPem, certificatePem, mine != nullptr ? Utf8(mine->Password()) : std::string(),
			secure->VerifyServerCertificate(), port == 465, error)) {
		case ibSmtpError::None:
			break;
		case ibSmtpError::Certificate:
			ibBackendCoreException::Error(_("InternetMail: the certificate file '%s' could not be read - not a PEM, or the key's password is wrong"),
				mine != nullptr ? mine->Path() : ibString());
			break;
		default:
			if (trusted != nullptr)
				ibBackendCoreException::Error(_("InternetMail: the trusted certificates could not be read from the certificate file '%s' - not a PEM"), trusted->Path());
			ibBackendCoreException::Error(_("InternetMail: the platform's trusted certificates could not be read - %s; name the authority's file in TrustedCertificates"),
				ibString::FromUTF8(error));
		}
	}

	std::string error;
	const ibSmtpError result = client->Logon(Utf8(profile.GetUser()), Utf8(profile.GetPassword()),
		std::chrono::seconds(m_timeout), error);
	if (result != ibSmtpError::None)
		ibBackendCoreException::Error(_("InternetMail: logging on to %s failed: %s"), m_server, Why(result, error, m_timeout));
	m_client = std::move(client);
}

// ONE MESSAGE, to every recipient or to none. A connection lost on the way logs off: the next Send is refused until
// the next Logon, rather than spoken over a socket nobody holds.
void ibValueInternetMail::Send(const ibValueInternetMailMessage& message)
{
	if (!m_client || !m_client->IsLoggedOn()) {
		m_client.reset();
		ibBackendCoreException::Error(_("InternetMail: not logged on - Logon(profile) first"));
	}
	ibMailMessage mail;
	message.Fill(mail);

	std::string error;
	const ibSmtpError result = m_client->Send(mail, std::chrono::seconds(m_timeout), error);
	if (result != ibSmtpError::None)
		ibBackendCoreException::Error(_("InternetMail: the message was not sent through %s: %s"), m_server, Why(result, error, m_timeout));
}

void ibValueInternetMail::Logoff()
{
	if (m_client)
		m_client->Logoff(std::chrono::seconds(m_timeout));
	m_client.reset();
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueInternetMailProfile, "InternetMailProfile", value_to_clsid("VL_IMPF"));
VALUE_TYPE_REGISTER(ibValueInternetMailMessage, "InternetMailMessage", value_to_clsid("VL_IMMS"));
VALUE_TYPE_REGISTER(ibValueInternetMail, "InternetMail", value_to_clsid("VL_IMAL"));
SYSTEM_TYPE_REGISTER(ibValueInternetMailTexts, "InternetMailTexts", system_to_clsid("VL_IMTS"));
SYSTEM_TYPE_REGISTER(ibValueInternetMailText, "InternetMailText", system_to_clsid("VL_IMTX"));
SYSTEM_TYPE_REGISTER(ibValueInternetMailAttachments, "InternetMailAttachments", system_to_clsid("VL_IMAS"));
SYSTEM_TYPE_REGISTER(ibValueInternetMailAttachment, "InternetMailAttachment", system_to_clsid("VL_IMAT"));

ENUM_TYPE_REGISTER(ibValueEnumInternetMailTextType, "InternetMailTextType", enum_to_clsid("EN_IMTT"));
