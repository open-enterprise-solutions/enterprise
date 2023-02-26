/****************************************************************************

 Project     :
 Author      :
 Description :

 VERSION INFORMATION:
 File    : $Source$
 Version : $Revision$
 Date    : $Date$
 Author  : $Author$
 Licence : wxWidgets licence

 History:
 $Log: wxemail.h,v $
 Revision 1.4  2003/11/14 15:43:09  tavasti
 Sending email with alternatives works

 Revision 1.3  2003/11/13 17:12:15  tavasti
 - Muutettu tiedostojen nimet wx-alkuisiksi

 Revision 1.2  2003/11/07 09:17:40  tavasti
 - K‰‰ntyv‰ versio, fileheaderit lis‰tty.


****************************************************************************/

/*
 * Purpose: wxWindows email implementation
 * Author:  Frank Buﬂ
 * Created: 2002
 */

#ifndef _WX_EMAIL_H
#define _WX_EMAIL_H

#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/wfstream.h>

#include "wxsmtpdef.h"

#include <list>
#include <vector>

#include <3rdparty/email/mimetic/mimeentity.h>

 /**
  * An email without transport mechanism specific declarations
  * or implementations.
  *
  * When the message is sent, the transport mechanism manipulates the
  * recipient disposition lists via OnRecipientStatus and OnMessageStatus
  * so that the necessary retry state information is maintained in this
  * object.
  */

class WXDLLIMPEXP_SMTP wxEmailMessage
{
public:
	/*!
	 * This type allows identifying a pop3 message.
	 *
	 * Note that some servers implement unique identifiers. In this
	 * case, the provided id is the same for a given message from one
	 * pop3 session to another and is guaranteed to be unique. So, a
	 * message can be use to uniquely identify this message.
	 *
	 * Note that unicity is only guaranteed for a given server, with a
	 * given pop account.
	 *
	 * Note also that some servers do not provide this mechanism. In this
	 * case, the returned ID uniquely identifies a message for a given session,
	 * but this is not guaranteed between different sessions.
	 */
	typedef struct MessageId
	{
		bool     is_unique;
		wxString id;
		void*    user_data;
		MessageId(bool is_unique = false, wxString id = _T(""), void* user_data = NULL)
			:is_unique(is_unique), id(id), user_data(user_data) {}
	} MessageId;

	/*!
	 * This class represents an e-mail address.
	 *
	 * It contains two fields :
	 *    \li \c address The mail address (ex. name@domain.com). This field is mandatory.
	 *    \li \c name The name of the address proprietary (ex. John Doe). This field is optional
	 */
	class Address
	{
	public:
		/*!
		 * Default constructor
		 *
		 * \param address The mail address
		 * \param name The name of the owner of the address
		 * \todo it should be nice to check address format...
		 */
		Address(const wxString& address = _T(""), const wxString& name = _T("")) : address(address), name(name) {}

		/*!
		 * Retrieves the mail address
		 *
		 * \return The mail address
		 */
		const wxString& GetAddress() const { return address; }

		/*!
		 * Retrieves the name of the owner of the address the address
		 *
		 * \return The name of the owner of the address, empty if not set
		 */
		const wxString& GetName() const { return name; }

		/*!
		 * Allows determining if a name exists
		 *
		 * \return true if a name exists, false otherwise
		 */
		bool HasName() const { return !name.IsEmpty(); }

		/*!
		 * \internal
		 *
		 * Returns an encoded MIME mail address
		 * \return the MIME encoded mail address
		 * \todo We should add coding
		 */
		wxString GetMimeStr() const
		{
			if (HasName())
			{
				return name + _T(" <") + address + _T(">");
			}
			else
			{
				return _T("<") + address + _T(">");
			}
		}

	private:

		wxString address;
		wxString name;
	};

	/**
	 * Constructs a new email.
	 *
	 * \param from The ::Address of the sender of the mail
	 * \param subject The subject of the mail (in default system encoding charset)
	 * \param text The body text of the mail (in default system encoding charset)
	 */
	wxEmailMessage(const Address& from = Address(),
		const wxString& subject = _T(""),
		const wxString& text = _T(""));

	/*!
	 * \internal
	 * Default destructor -> virtual to avoid problems...
	 */
	virtual ~wxEmailMessage() {}

	/*!
	 * Returns the subject associated to this mail
	 *
	 * \return the subject of the mail, in default system encoding charset
	 */
	const wxString& GetSubject() const { return m_subject; }

	/*!
	 * Returns the body associated to this mail
	 *
	 * \return the body of the mail, in default system encoding charset
	 */
	const wxString& GetBody() const { return m_text; }

	/**
	 * Gets the sender address of the message
	 *
	 * \return The sender address
	 */
	const Address& GetFrom() const;

	/**
	 * Adds an additional recipient in the to-field.
	 * \param address The email address of the recipient.
	 */
	virtual void AddTo(const Address& address);

	/*!
	 * Returns the list of all addresses put as to recipients of this mail
	 * \return The list of all to recipients
	 */
	const std::list<Address>& GetToList() const { return m_toArray; }

	/**
	 * Adds an additional recipient in the cc-field.
	 * \param address The email address of the recipient.
	 */
	virtual void AddCc(const Address& address);

	/*!
	 * Returns the list of all addresses put as cc recipients of this mail
	 * \return The list of all cc recipients
	 */
	const std::list<Address>& GetCcList() const { return m_ccArray; }

	/**
	 * Adds an additional recipient in the bcc-field.
	 * \param address The email address of the recipient.
	 */
	virtual void AddBcc(const Address& address);

	/*!
	 * Returns the list of all addresses put as bcc recipients of this mail
	 * \return The list of all bcc recipients
	 */
	const std::list<Address>& GetBccList() const { return m_bccArray; }

	/*!
	 * This function allows assigning an HTML content to the mail.
	 *
	 * If an HTML content is already present, it is overriden with this new one.
	 *
	 * \param the alternative HTML content.
	 */
	void AddAlternativeHtmlBody(const wxString data);

	/*!
	 * Indicates if an alternative html body is available for this message
	 * \return true if this message has an alternative html body, false otherwise
	 */
	bool HasHtmlBody() const { return has_html_alternative; }

	/*!
	 * Returns the alternative HTML body, if any.
	 *
	 * This function will return an empty string if no alternative html body exists
	 *
	 * \return the alternative HTML body
	 */
	const wxString& GetHtmlBody() const { return html_alternative; }

	/*!
	 * This function returns the date and time of the mail
	 *
	 */
	const wxDateTime& GetDateTime() const { return m_date_time; }

	/*!
	 * This function returns the list of all attachments this mail contains.
	 *
	 * \return The list of all attachments names.
	 */
	const std::vector<wxString>& GetAttachmentsNames() const { return attachments_names; }

	/*!
	 * This function returns the list of all attachments MIME ids.
	 * Note that those Ids can be used by received mails to perform intra-mail
	 * references.
	 *
	 * Note that if an attachment has no MIME id, the returned ID is an empty string.
	 *
	 * \return The list of all attachments MIME Ids.
	 */
	const std::vector<wxString>& GetAttachmentsMimeIds() const { return attachments_MIME_ids; }

	/**
	 * Adds an attachment to the mail.
	 *
	 * \param fileName wxFilename of the new file attachment.
	 * \param name the name given to the attachment. If empty, we take the name of the file
	 */
	void AddAttachment(const wxFileName& fileName, const wxString name = _T(""), const wxString& mime_id = _T(""));

	/*!
	 * This function returns true if there is at least ont attachment to the mail,
	 * false otherwise
	 */
	bool HasAttachments() const { return attachments_names.size() != 0; }

	/*!
	 * This function extracts the attachment with 'id' and puts its content in a file
	 * with provided filename
	 *
	 * \param fileName the name of the file in which attachment shall be extracted
	 * \param id the id of the attachment that shall be extracted (matches the id of attachment in list returned by ::GetAttachmentsNames).
	 */
	void ExtractAttachment(const wxFileName& fileName, unsigned long id) const;

	/*!
	 * This function returns the ::MessageId associated to this message
	 *
	 * \return the ::MessageId of the message
	 */
	const MessageId& GetMessageId() const { return message_id; }

	/*!
	 * This function allows assigning an id to the message.
	 */
	void SetMessageId(const MessageId& message_id) { this->message_id = message_id; }

	/*!
	 * This function returns the size of the message;
	 *
	 * \return the size of the message, in bytes
	 */
	unsigned long GetSize() const;

	/*!
	 * This function saves the content of the message in provided file
	 *
	 * \param The name of the file where message shall be saved
	 */
	void SaveToFile(const wxFileName& file_name);

	/*!
	 * This function returns a wxEmailMessage constructed with content of file
	 * pointed by file_name
	 *
	 * \param file_name the name of the file that shall be loaded
	 * \return the constructed wxEMailMessage
	 */
	static wxEmailMessage LoadFromFile(const wxFileName& file_name);

	/*!
	 * \internal
	 *
	 * This constructor allows constructing an EMail message from a MIME coded
	 * string.
	 *
	 * \param message_content the MIME encoded message content
	 * \param message_id the ID of the message
	 * \param shall_extract_full_message if true, we extract the complete message, else, we only extract header.
	 */
	wxEmailMessage(const wxString& message_content,
		const MessageId& message_id,
		bool shall_extract_full_message);

protected:

	void LoadMimeContent(bool shall_extract_full_message = true);
	static void MIMEExtractBody(mimetic::MimeEntity& entity, std::ostream& stream, bool shall_recode);
	void MIMEExtractAll(mimetic::MimeEntity& entity, bool& found_plain_text_body, bool& found_html_body);

	static void SaveStringInFile(wxFileOutputStream& stream, const wxString& str);
	static wxString LoadStringFromFile(wxFileInputStream& stream);

	wxString m_subject;
	wxString m_text;
	Address m_from;

	wxDateTime m_date_time;

	std::list<Address> m_toArray;
	std::list<Address> m_ccArray;
	std::list<Address> m_bccArray;

	MessageId message_id;

	bool has_html_alternative;
	wxString html_alternative;

	std::vector<wxString> attachments_names;
	std::vector<wxString> attachments_MIME_ids;
	std::vector<std::vector<unsigned char> > attachments_contents;

	bool has_message_MIME_content;
	wxString message_MIME_content;
};


#endif
