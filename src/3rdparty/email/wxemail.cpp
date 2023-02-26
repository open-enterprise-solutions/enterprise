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
 $Log: wxemail.cpp,v $
 Revision 1.7  2004/05/19 04:06:26  tavasti
 Fixes based on comments from Edwards John-BLUW23 <jedwards@motorola.com>
 - Removed -m486 flags from makefile
 - Added filetypes wav & mp3
 - Removed default arguments from wxmime.cpp (only in .h)
 - Commented out iostream.h includes

 Revision 1.6  2003/11/21 12:36:46  tavasti
 - Makefilet -Wall optioilla
 - Korjattu 'j‰rkev‰t' varoitukset pois (J‰‰nyt muutama joita ei saa
   kohtuudella poistettua)

 Revision 1.5  2003/11/14 15:46:50  tavasti
 Commented out some debug printing

 Revision 1.4  2003/11/14 15:43:09  tavasti
 Sending email with alternatives works

 Revision 1.3  2003/11/13 17:12:15  tavasti
 - Muutettu tiedostojen nimet wx-alkuisiksi

 Revision 1.2  2003/11/07 09:17:40  tavasti
 - K‰‰ntyv‰ versio, fileheaderit lis‰tty.


****************************************************************************/

//static char cvs_id[] = "$Header: /v/CVS/olive/notifier/wxSMTP/src/wxemail.cpp,v 1.2 2004/09/07 09:02:35 paul Exp $";

/*
 * Purpose: wxWindows email implementation
 * Author:  Frank Buﬂ
 * Created: 2002
 */

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#ifdef __BORLANDC__
#pragma hdrstop
#endif

// includes
#ifndef WX_PRECOMP
   // here goes the #include <wx/abc.h> directives for those
   // files which are not included by wxprec.h
#endif

#include <3rdparty/email/wxemail.h>

#include <3rdparty/email/codec/rfc2231.h>
#include <3rdparty/email/codec/rfc2047.h>
#include <3rdparty/email/codec/charsetconv.h>

#define CURRENT_FILE_ID 1

wxEmailMessage::wxEmailMessage(const Address& from,
                               const wxString& subject,
                               const wxString& text)
               :m_subject(subject),
                m_text(text),
                m_from(from),
                has_html_alternative(false),
                has_message_MIME_content(false)
{
   //TODO : il faufrait le coder dans l'envoi smtp
   message_id.is_unique = false;
   message_id.id = _T("");
   m_date_time = wxDateTime::Now();
}

void wxEmailMessage::MIMEExtractBody(mimetic::MimeEntity& entity, std::ostream& stream, bool shall_recode)
{
   /* Try to get type */
   wxString encoding_type;
   if (entity.header().hasField("Content-Transfer-Encoding"))
   {
      encoding_type = wxString(entity.header().field("Content-Transfer-Encoding").value().c_str(), wxConvLocal);
   }

   /* Get charset */
   wxString charset = wxRfc2231::Decode(entity.header().contentType().paramList(),
                                        _T("charset"));

   /* Try identifying the type */
   if (encoding_type.CmpNoCase(_T("7bit")) == 0)
   {
      /* No need to perform conversion : everything is ASCII character... */
      stream << entity.body().c_str();
   }
   else if (encoding_type.CmpNoCase(_T("8bit")) == 0)
   {
      /* Extract content */
      wxString result = wxCharsetConverter::ConvertCharset(wxString(entity.body().c_str(), wxConvLocal), charset);

      /* Send it to stream */
      stream << result.mb_str(wxConvLocal);
   }
   else if (encoding_type.CmpNoCase(_T("base64")) == 0)
   {
      /* Instanciate decoder */
      mimetic::Base64::Decoder b64;

      if (shall_recode)
      {
         /* Decode base 64 data */
         std::vector<unsigned char> buffer;
         mimetic::decode(entity.body().begin(),
                         entity.body().end(),
                         b64,
                         std::back_inserter(buffer));
         buffer.push_back(0);

         /* Flush content in a string */
         wxString content((const char*)&buffer[0], wxConvLocal);

         /* Convert it to proper charset */
         wxString result = wxCharsetConverter::ConvertCharset(content, charset);

         /* Send it to stream */
         stream << result.mb_str(wxConvLocal);
      }
      else
      {
         std::vector<unsigned char> buffer;

         mimetic::decode(entity.body().begin(),
                         entity.body().end(),
                         b64,
                         std::back_inserter(buffer));
         buffer.push_back(0);

         for (unsigned char* p = &buffer[0]; p != &buffer[buffer.size()-1]; p++)
         {
            stream << *p;
         }
      }
   }
   else if (encoding_type.CmpNoCase(_T("quoted-printable")) == 0)
   {
      /* Instanciate decoder */
      mimetic::QP::Decoder qp;

      /* Decode base 64 data */
      std::vector<unsigned char> buffer;
      mimetic::decode(entity.body().begin(),
                      entity.body().end(),
                      qp,
                      std::back_inserter(buffer));
      buffer.push_back(0);

      /* Flush content in a string */
      wxString content;
      for (unsigned char* p = &buffer[0]; p != &buffer[buffer.size()-1]; p++)
      {
         content.Append(*p, 1);
      }

      /* Convert it to proper charset */
      wxString result = wxCharsetConverter::ConvertCharset(content, charset);

      /* Send it to stream */
      stream << result.mb_str(wxConvLocal);
   }
   else if (encoding_type.CmpNoCase(_T("binary")) == 0)
   {
      /* Does not seem to be a valid encoding scheme in current MIME definition -> we will return data as is... */
      stream << entity.body().c_str();
   }
   else if (encoding_type.CmpNoCase(_T("ietf-token")) == 0)
   {
      //TODO : to implement : ietf-token format
      stream << _T("Not supported type ") << encoding_type.c_str();
   }
   else if (encoding_type.CmpNoCase(_T("x-token")) == 0)
   {
      //TODO : to implement : x-token format
      stream << _T("Not supported type ") << encoding_type.c_str() << "!";
   }
   else
   {
      /* Not supported encoding type -> we will provide content as is as some e-mails are not properly formatted... */
      stream << entity.body().c_str();
   }
}

void wxEmailMessage::MIMEExtractAll(mimetic::MimeEntity& entity, bool& found_plain_text_body, bool& found_html_body)
{
   mimetic::Header& h = entity.header();                   // get header object

   if (!h.contentType().isMultipart())
   {
      /* get type/subtype */
      wxString type_subtype = wxString(h.contentType().type().c_str(), wxConvLocal) << _T("/") <<
            wxString(h.contentType().subtype().c_str(), wxConvLocal);
      if ((type_subtype.CmpNoCase(_T("text/plain")) == 0) &&
          (!found_plain_text_body))
      {
         found_plain_text_body = true;

         std::ostringstream os;
         MIMEExtractBody(entity, os, true);

         m_text = wxString(os.str().c_str(), wxConvLocal);
      }
      else if ((type_subtype.CmpNoCase(_T("text/html")) == 0) &&
               (!found_html_body))
      {
         found_html_body = true;

         std::ostringstream os;
         MIMEExtractBody(entity, os, true);

         has_html_alternative = true;
         html_alternative = wxString(os.str().c_str(), wxConvLocal);
      }
      else
      {
         /* We will consider this as an attachment */

         /* retrieve attachment name */
         wxString original_name = wxRfc2231::Decode(entity.header().contentDisposition().paramList(),
                                                    _T("filename"));

         /* Get temporary file with this name */
         wxString temp_file_name = wxFileName::CreateTempFileName(original_name);

         /* Open the file as an std output stream */
         std::ofstream file_stream;
         file_stream.open((const char*)temp_file_name.mb_str(wxConvFile),
                          std::ios_base::out|
                             std::ios_base::trunc|
                             std::ios_base::binary);

         /* Extract content in file */
         MIMEExtractBody(entity, file_stream, false);

         /* Close the file */
         file_stream.close();

         /* flush attachment */
         AddAttachment(temp_file_name, original_name, wxString(entity.header().contentId().str().c_str(), wxConvLocal).AfterFirst('<').BeforeLast('>'));
      }
   }

   mimetic::MimeEntityList& parts = entity.body().parts(); // list of sub entities obj
   // cycle on sub entities list and print info of every item
   mimetic::MimeEntityList::iterator mbit = parts.begin(), meit = parts.end();
   for(; mbit != meit; ++mbit)
   {
      MIMEExtractAll(**mbit, found_plain_text_body, found_html_body);
   }
}

wxEmailMessage::wxEmailMessage(const wxString& message_content, const MessageId& message_id, bool shall_extract_full_message)
               :message_id(message_id),
                has_html_alternative(false),
                has_message_MIME_content(true),
                message_MIME_content(message_content)
{
   LoadMimeContent(shall_extract_full_message);
}

void wxEmailMessage::LoadMimeContent(bool shall_extract_full_message)
{
   /* Get the mimeentity */
   std::string stream_str((const char*)message_MIME_content.mb_str(wxConvLocal));
   std::istringstream stream(stream_str);
   mimetic::MimeEntity entity(stream);

   /* Get from */
   if (entity.header().from().size() > 0)
   {
      m_from = Address(wxString(entity.header().from().front().mailbox().c_str(), wxConvLocal)+_T("@")+wxString(entity.header().from().front().domain().c_str(), wxConvLocal),
                       wxRfc2047::Decode(wxString(entity.header().from().front().label().c_str(), wxConvLocal)));
   }

   /* Get subject */
   m_subject = wxRfc2047::Decode(wxString(entity.header().subject().c_str(), wxConvLocal));

   /* Extract date */
   if (entity.header().hasField("Date"))
   {
      /* Try RFC 822 format*/
      if (m_date_time.ParseRfc822Date(wxString(entity.header().field("Date").value().c_str(), wxConvLocal)) == NULL)
      {
         /* If it fails, try a less-restrictive format */
         if (m_date_time.ParseDateTime(wxString(entity.header().field("Date").value().c_str(), wxConvLocal)) == NULL)
         {
            /* OK, set current time then... */
            m_date_time = wxDateTime::Now();
         }
      }
   }
   else
   {
      m_date_time = wxDateTime::Now();
   }

   /* Extract all to addresses */
   std::vector<mimetic::Address>::iterator to_it;
   for (to_it=entity.header().to().begin(); to_it != entity.header().to().end(); to_it++)
   {
      AddTo(Address(wxString(to_it->mailbox().mailbox().c_str(), wxConvLocal)+_T("@")+wxString(to_it->mailbox().domain().c_str(), wxConvLocal),
                    wxRfc2047::Decode(wxString(to_it->mailbox().label().c_str(), wxConvLocal))));
   }

   /* Extract all cc addresses */
   std::vector<mimetic::Address>::iterator cc_it;
   for (cc_it=entity.header().cc().begin(); cc_it != entity.header().cc().end(); cc_it++)
   {
      AddCc(Address(wxString(cc_it->mailbox().mailbox().c_str(), wxConvLocal)+_T("@")+wxString(cc_it->mailbox().domain().c_str(), wxConvLocal),
                    wxRfc2047::Decode(wxString(cc_it->mailbox().label().c_str(), wxConvLocal))));
   }

   /* Extract all bcc addresses */
   std::vector<mimetic::Address>::iterator bcc_it;
   for (bcc_it=entity.header().bcc().begin(); bcc_it != entity.header().bcc().end(); bcc_it++)
   {
      AddBcc(Address(wxString(bcc_it->mailbox().mailbox().c_str(), wxConvLocal)+_T("@")+wxString(bcc_it->mailbox().domain().c_str(), wxConvLocal),
                     wxRfc2047::Decode(wxString(bcc_it->mailbox().label().c_str(), wxConvLocal))));
   }

   /* Check if we shall extract full message */
   if (shall_extract_full_message)
   {
      /* Parse all parts of this mail */
      bool found_plain_text_body = false;
      bool found_html_body = false;
      MIMEExtractAll(entity, found_plain_text_body, found_html_body);
   }
}

const wxEmailMessage::Address& wxEmailMessage::GetFrom() const
{
   return m_from;
}

void wxEmailMessage::AddTo(const Address& address)
{
   m_toArray.push_back(address);
}

void wxEmailMessage::AddCc(const Address& address)
{
   m_ccArray.push_back(address);
}

void wxEmailMessage::AddBcc(const Address& address)
{
   m_bccArray.push_back(address);
}

void wxEmailMessage::AddAlternativeHtmlBody(const wxString data)
{
   has_html_alternative = true;
   html_alternative = data;
}

void wxEmailMessage::AddAttachment(const wxFileName& fileName, const wxString name, const wxString& mime_id)
{
   /* Check if file exists */
   if (!fileName.FileExists())
   {
      return;
   }

   /* extract file content */
   wxFileInputStream stream(fileName.GetFullPath());
   std::vector<unsigned char> content;
   while (1)
   {
      int c = stream.GetC();
      if (c == wxEOF)
         break;
      content.push_back(c);
   }

   /* Flush file content */
   attachments_contents.push_back(content);

   /* Flush the name */
   if (name.IsEmpty())
   {
      attachments_names.push_back(fileName.GetFullPath());
   }
   else
   {
      attachments_names.push_back(name);
   }

   /* Flush the MIME id */
   attachments_MIME_ids.push_back(mime_id);
}

void wxEmailMessage::ExtractAttachment(const wxFileName& fileName, unsigned long id) const
{
   wxFileOutputStream stream(fileName.GetFullPath());

   std::vector<unsigned char>::const_iterator it;
   for (it = attachments_contents.at(id).begin(); it != attachments_contents.at(id).end(); it++)
   {
      stream.PutC(*it);
   }
}

unsigned long wxEmailMessage::GetSize() const
{
   /* check if we have MIME content */
   if (has_message_MIME_content)
   {
      return message_MIME_content.size();
   }
   else
   {
      unsigned long size = m_subject.size() +
                           m_text.size() +
                           m_from.GetAddress().size()+
                           m_from.GetName().size()+
                           html_alternative.size();

      std::list<Address>::const_iterator it;
      for (it = m_toArray.begin(); it != m_toArray.end(); it++)
         size += it->GetAddress().size() + it->GetName().size();
      for (it = m_ccArray.begin(); it != m_ccArray.end(); it++)
         size += it->GetAddress().size() + it->GetName().size();
      for (it = m_bccArray.begin(); it != m_bccArray.end(); it++)
         size += it->GetAddress().size() + it->GetName().size();

      std::vector<std::vector<unsigned char> >::const_iterator it_2;
      for (it_2 = attachments_contents.begin(); it_2 != attachments_contents.end(); it_2++)
         size += it_2->size();

      return size;
   }
}

void wxEmailMessage::SaveStringInFile(wxFileOutputStream& stream, const wxString& str)
{
   unsigned long size = str.size()+1;
   stream.Write(&size, sizeof(size));
   stream.Write(str.c_str(), size);
}

wxString wxEmailMessage::LoadStringFromFile(wxFileInputStream& stream)
{
   unsigned long size;
   stream.Read(&size, sizeof(size));
   char* buffer = new char[size];
   stream.Read(buffer, size);
   wxString result(buffer, wxConvLocal);
   delete[] buffer;
   return result;
}

void wxEmailMessage::SaveToFile(const wxFileName& file_name)
{
   /* open the output stream */
   wxFileOutputStream stream(file_name.GetFullPath());

   /* Put the current version ID */
   unsigned long version_id = CURRENT_FILE_ID;
   stream.Write(&version_id, sizeof(version_id));

   /* put content of flag telling if MIME content exists */
   stream.Write(&has_message_MIME_content, sizeof(has_message_MIME_content));

   /* Check which version shall be saved */
   if (has_message_MIME_content)
   {
      /* We simply load MIME message content */
      SaveStringInFile(stream, message_MIME_content);
   }
   else
   {
      /* We flush all elements */
      SaveStringInFile(stream, m_subject);
      SaveStringInFile(stream, m_text);
      SaveStringInFile(stream, m_from.GetAddress());
      SaveStringInFile(stream, m_from.GetName());

      SaveStringInFile(stream, m_date_time.Format(_T("%a, %d %b %Y %H:%M:%S %z")));

      unsigned long nb_elements = m_toArray.size();
      stream.Write(&nb_elements, sizeof(nb_elements));
      std::list<Address>::const_iterator it;
      for (it = m_toArray.begin(); it != m_toArray.end(); it++)
      {
         SaveStringInFile(stream, it->GetAddress());
         SaveStringInFile(stream, it->GetName());
      }

      nb_elements = m_ccArray.size();
      stream.Write(&nb_elements, sizeof(nb_elements));
      for (it = m_ccArray.begin(); it != m_ccArray.end(); it++)
      {
         SaveStringInFile(stream, it->GetAddress());
         SaveStringInFile(stream, it->GetName());
      }

      nb_elements = m_bccArray.size();
      stream.Write(&nb_elements, sizeof(nb_elements));
      for (it = m_bccArray.begin(); it != m_bccArray.end(); it++)
      {
         SaveStringInFile(stream, it->GetAddress());
         SaveStringInFile(stream, it->GetName());
      }

      stream.Write(&has_html_alternative, sizeof(has_html_alternative));
      SaveStringInFile(stream, html_alternative);

      nb_elements = attachments_names.size();
      stream.Write(&nb_elements, sizeof(nb_elements));

      std::vector<wxString>::const_iterator it2;
      for (it2 = attachments_names.begin(); it2 != attachments_names.end(); it2++)
      {
         SaveStringInFile(stream, *it2);
      }

      for (it2 = attachments_MIME_ids.begin(); it2 != attachments_MIME_ids.end(); it2++)
      {
         SaveStringInFile(stream, *it2);
      }

      std::vector<std::vector<unsigned char> >::const_iterator it3;
      for (it3 = attachments_contents.begin(); it3 != attachments_contents.end(); it3++)
      {
         unsigned long size = it3->size();
         stream.Write(&size, sizeof(size));
         for (unsigned long i = 0; i < size; i ++)
         {
            stream.PutC(it3->at(i));
         }
      }

      stream.Write(&message_id.is_unique, sizeof(message_id.is_unique));
      SaveStringInFile(stream, message_id.id);
   }
}

wxEmailMessage wxEmailMessage::LoadFromFile(const wxFileName& file_name)
{
   /* create resulting message */
   wxEmailMessage msg;

   /* open the output stream */
   wxFileInputStream stream(file_name.GetFullPath());

   /* Get the current version ID */
   unsigned long version_id;
   stream.Read(&version_id, sizeof(version_id));
   if (version_id != CURRENT_FILE_ID)
   {
      wxASSERT(0);
      return msg;
   }

   /* get content of flag telling if MIME content exists */
   stream.Read(&msg.has_message_MIME_content, sizeof(msg.has_message_MIME_content));

   /* Check which version shall be saved */
   if (msg.has_message_MIME_content)
   {
      /* We simply load MIME message content */
      msg.message_MIME_content = LoadStringFromFile(stream);

      /* OK, now generate corresponding message */
      msg.LoadMimeContent();
   }
   else
   {
      /* We flush all elements */
      msg.m_subject = LoadStringFromFile(stream);
      msg.m_text = LoadStringFromFile(stream);
      wxString from_addr = LoadStringFromFile(stream);
      wxString from_name = LoadStringFromFile(stream);
      msg.m_from = Address(from_addr, from_name);

      wxString date_str = LoadStringFromFile(stream);
      msg.m_date_time.ParseRfc822Date(date_str.c_str());

      unsigned long nb_elements;
      stream.Read(&nb_elements, sizeof(nb_elements));
      for (unsigned int i = 0; i < nb_elements; i++)
      {
         wxString address = LoadStringFromFile(stream);
         wxString name = LoadStringFromFile(stream);
         msg.m_toArray.emplace_back(address, name);
      }

      stream.Read(&nb_elements, sizeof(nb_elements));
      for (unsigned int i = 0; i < nb_elements; i++)
      {
         wxString address = LoadStringFromFile(stream);
         wxString name = LoadStringFromFile(stream);
         msg.m_ccArray.emplace_back(address, name);
      }

      stream.Read(&nb_elements, sizeof(nb_elements));
      for (unsigned int i = 0; i < nb_elements; i++)
      {
         wxString address = LoadStringFromFile(stream);
         wxString name = LoadStringFromFile(stream);
         msg.m_bccArray.emplace_back(address, name);
      }

      stream.Read(&msg.has_html_alternative, sizeof(msg.has_html_alternative));
      msg.html_alternative = LoadStringFromFile(stream);

      stream.Read(&nb_elements, sizeof(nb_elements));
      for (unsigned int i = 0; i < nb_elements; i++)
      {
         wxString attachment_name = LoadStringFromFile(stream);
         msg.attachments_names.push_back(attachment_name);
      }

      for (unsigned int i = 0; i < nb_elements; i++)
      {
         wxString attachment_mime_id = LoadStringFromFile(stream);
         msg.attachments_MIME_ids.push_back(attachment_mime_id);
      }

      for (unsigned int i = 0; i < nb_elements; i++)
      {
         std::vector<unsigned char> att_content;
         unsigned long att_content_size;
         stream.Read(&att_content_size, sizeof(att_content_size));
         for (unsigned int j = 0; j < att_content_size; j++)
         {
            att_content.push_back(stream.GetC());
         }
         msg.attachments_contents.push_back(att_content);
      }

      stream.Read(&msg.message_id.is_unique, sizeof(msg.message_id.is_unique));
      msg.message_id.id = LoadStringFromFile(stream);
   }

   return msg;
}
