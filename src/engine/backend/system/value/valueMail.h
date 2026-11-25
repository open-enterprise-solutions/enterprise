#ifndef __VALUE_MAIL_H__
#define __VALUE_MAIL_H__

// ⭐ MAIL FROM A SCRIPT — a profile says which server and who logs on, a message says what goes and to whom, and
// InternetMail sends it:
//
//     var profile = New InternetMailProfile();
//     profile.SMTPServerAddress = "smtp.example"; profile.SMTPUser = "shop"; profile.SMTPPassword = password;
//     profile.SecureConnection = New SecureConnection();       // TLS: from the first byte on 465, STARTTLS elsewhere
//     var message = New InternetMailMessage();
//     message.From = "Shop <shop@example.com>"; message.To.Add("buyer@example.com"); message.Subject = "Invoice 42";
//     message.Texts.Add("The invoice is attached.");
//     message.Attachments.Add("C:\out\invoice-42.pdf");
//     var mail = New InternetMail(); mail.Logon(profile); mail.Send(message); mail.Logoff();
//
// SENDING ONLY (SMTP). The conversation and the MIME are the core's (core/net/smtpClient.h), its TLS the one HTTPS
// speaks. A failure raises, naming the server and why — a recipient the server refuses among them, and then nothing
// is sent: a message goes to every recipient or to none.

#include "backend/compiler/value.h"
#include "backend/system/value/valueSecureConnection.h"

#include "core/memory/memoryBuffer.h"

#include <memory>

class ibMailMessage;   // core/net/smtpClient.h
class ibSmtpClient;

// WHAT A TEXT OF A MESSAGE IS — plain text, or HTML the reader's program shows as a page.
enum ibInternetMailTextType {
	ibInternetMailTextType_PlainText,
	ibInternetMailTextType_HTML,
};

void ibValueInternetMailProfile_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// WHICH SERVER AND WHO — read by Logon, and only then: a profile changed after it changes nothing until the next one.
// The password is given and never read back. The port, when none is given, is the one the security says: 465 with a
// SecureConnection (TLS from the first byte), 25 without; on any other port a SecureConnection is STARTTLS.
class BACKEND_API ibValueInternetMailProfile : public ibValueStaticMembers<&ibValueInternetMailProfile_BindNames> {
	enum Prop {
		enSMTPServerAddress,
		enSMTPPort,
		enSMTPUser,
		enSMTPPassword,
		enTimeout,
		enSecureConnection,
	};
public:

	ibValueInternetMailProfile();
	virtual ~ibValueInternetMailProfile();

	virtual bool Init() { return true; }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);

	const ibString& GetServer() const { return m_server; }
	int GetPort() const;                                  // the one given, or the security's
	const ibString& GetUser() const { return m_user; }
	const ibString& GetPassword() const { return m_password; }
	int GetTimeout() const { return m_timeout; }
	const ibValueSecureConnection* GetSecureConnection() const { return m_secure; }

private:
	ibString                            m_server;
	int                                 m_port = 0;         // 0 - not given
	ibString                            m_user;
	ibString                            m_password;
	int                                 m_timeout = 30;
	ibValuePtr<ibValueSecureConnection> m_secure;           // null - in the clear
};

void ibValueInternetMailText_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ONE TEXT OF A MESSAGE, as Texts.Add made it.
class BACKEND_API ibValueInternetMailText : public ibValueStaticMembers<&ibValueInternetMailText_BindNames> {
	enum Prop {
		enText,
		enTextType,
	};
public:

	ibValueInternetMailText();
	ibValueInternetMailText(const ibString& text, ibInternetMailTextType type);
	virtual ~ibValueInternetMailText();

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	const ibString& GetText() const { return m_text; }
	ibInternetMailTextType GetTextType() const { return m_type; }

private:
	ibString               m_text;
	ibInternetMailTextType m_type = ibInternetMailTextType_PlainText;
};

void ibValueInternetMailTexts_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// A MESSAGE'S TEXTS — counted, got and walked as a collection is; Add puts one more. The plain ones go as one text,
// in the order added, and so do the HTML ones; both — the reader's program shows the one it can.
class BACKEND_API ibValueInternetMailTexts : public ibValueCollection<ibValuePtr<ibValueInternetMailText>, &ibValueInternetMailTexts_BindNames> {
	enum Func {
		enAdd = enCollectionFuncs,
	};
public:

	ibValueInternetMailTexts();

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

protected:
	virtual ibValue ElementValue(const ibValuePtr<ibValueInternetMailText>& text) const override;
};

void ibValueInternetMailAttachment_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ONE ATTACHED FILE — the name the recipient sees and its bytes, taken when it was added.
class BACKEND_API ibValueInternetMailAttachment : public ibValueStaticMembers<&ibValueInternetMailAttachment_BindNames> {
	enum Prop {
		enName,
		enData,
	};
public:

	ibValueInternetMailAttachment();
	ibValueInternetMailAttachment(const ibString& name, const ibMemoryBuffer& bytes);
	virtual ~ibValueInternetMailAttachment();

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);

	const ibString& GetName() const { return m_name; }
	const ibMemoryBuffer& GetBytes() const { return m_bytes; }

private:
	ibString       m_name;
	ibMemoryBuffer m_bytes;
};

void ibValueInternetMailAttachments_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// A MESSAGE'S ATTACHMENTS — Add takes BinaryData or a file's name; a file is read when it is added.
class BACKEND_API ibValueInternetMailAttachments : public ibValueCollection<ibValuePtr<ibValueInternetMailAttachment>, &ibValueInternetMailAttachments_BindNames> {
	enum Func {
		enAdd = enCollectionFuncs,
	};
public:

	ibValueInternetMailAttachments();

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

protected:
	virtual ibValue ElementValue(const ibValuePtr<ibValueInternetMailAttachment>& attachment) const override;
};

void ibValueInternetMailMessage_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// A MESSAGE — who sends it, to whom (To, Cc, Bcc: arrays of `address` or `Name <address>`), its subject, texts and
// attachments. Bcc reaches its recipients and is seen by none of them.
class BACKEND_API ibValueInternetMailMessage : public ibValueStaticMembers<&ibValueInternetMailMessage_BindNames> {
	enum Prop {
		enFrom,
		enTo,
		enCc,
		enBcc,
		enSubject,
		enTexts,
		enAttachments,
	};
public:

	ibValueInternetMailMessage();
	virtual ~ibValueInternetMailMessage();

	virtual bool Init() { return true; }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);
	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);

	// The message in the core's words (UTF-8), as the client sends it. An address that is not a string is refused.
	void Fill(ibMailMessage& message) const;

private:
	ibString                                   m_from;
	ibValue                                    m_to;          // Arrays of `address` or `Name <address>`
	ibValue                                    m_cc;
	ibValue                                    m_bcc;
	ibString                                   m_subject;
	ibValuePtr<ibValueInternetMailTexts>       m_texts;
	ibValuePtr<ibValueInternetMailAttachments> m_attachments;
};

void ibValueInternetMail_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ONE SERVER'S CONNECTION — Logon(profile) opens it and logs on, Send(message) as many times as there are messages,
// Logoff() says goodbye. Let go while logged on, it says goodbye by itself.
class BACKEND_API ibValueInternetMail : public ibValueStaticMembers<&ibValueInternetMail_BindNames> {
	enum Func {
		enLogon,
		enSend,
		enLogoff,
	};
public:

	ibValueInternetMail();
	virtual ~ibValueInternetMail();

	virtual bool Init() override { return true; }

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

	// The same for a caller in C++ (and for the tests).
	void Logon(const ibValueInternetMailProfile& profile);
	void Send(const ibValueInternetMailMessage& message);
	void Logoff();

private:
	std::unique_ptr<ibSmtpClient> m_client;     // null - not logged on
	ibString                      m_server;     // host:port, as a refusal names it
	int                           m_timeout = 30;
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"

class ibValueEnumInternetMailTextType : public ibValueEnumeration<ibInternetMailTextType> {
public:
	ibValueEnumInternetMailTextType() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibInternetMailTextType_PlainText, ibT("PlainText"), ibTRANSLATE("Plain text"));
		AddEnumeration(ibInternetMailTextType_HTML,      ibT("HTML"),      ibTRANSLATE("HTML"));
	}
};
#pragma endregion

#endif // !__VALUE_MAIL_H__
