/////////////////////////////////////////////////////////////////////////////
// Name:        wxsmtpemail.cpp
// Purpose:     This class implements an e-mail ready to be sent by the smtp class
// Author:      Brice André
// Created:     2010/12/02
// RCS-ID:      $Id: mycomp.cpp 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2010 Brice André
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////


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

#include <email/mimetic/mimetic.h>
#include <email/smtp/wxsmtp.h>
#include <email/mimetic/rfc822/message.h>

wxSMTP::wxSmtpEmailMessage::wxSmtpEmailMessage()
{
   has_already_sent_recipients = false;
}

wxSMTP::wxSmtpEmailMessage::wxSmtpEmailMessage(const wxEmailMessage& e_mail_message)
                   :wxEmailMessage(e_mail_message),
                    has_already_sent_recipients(false)
{
   std::list<Address>::const_iterator it;
   for (it = m_toArray.begin(); it != m_toArray.end(); it++)
   {
      m_rcptAttempt.push_back(*it);
   }
   for (it = m_ccArray.begin(); it != m_ccArray.end(); it++)
   {
      m_rcptAttempt.push_back(*it);
   }
   for (it = m_bccArray.begin(); it != m_bccArray.end(); it++)
   {
      m_rcptAttempt.push_back(*it);
   }
}

bool wxSMTP::wxSmtpEmailMessage::RecipientsAlreadySent() const
{
   return has_already_sent_recipients;
}

bool wxSMTP::wxSmtpEmailMessage::GetNextRecipient(Address& rcpt)
{
   has_already_sent_recipients = true;
   if (m_rcptAttempt.empty())
      return FALSE;
   rcpt = m_rcptAttempt.front();
   m_rcptAttempt.pop_front();
   return TRUE;
}

bool wxSMTP::wxSmtpEmailMessage::HasStillRecipients() const
{
   return !m_rcptAttempt.empty();
}

void wxSMTP::wxSmtpEmailMessage::ReinsertRecipient(const wxEmailMessage::Address& rcpt)
{
   m_rcptAttempt.push_back(rcpt);
}

using namespace mimetic;
void wxSMTP::wxSmtpEmailMessage::Encode(wxOutputStream& out)
{
   /* Instanciate the message */
   Message me;

   /* Format the header */
   me.header().from((const char*)m_from.GetMimeStr().mb_str(wxConvLocal));

   AddressList to_list;
   std::list<Address>::iterator to_it;
   for ( to_it=m_toArray.begin() ; to_it != m_toArray.end(); to_it++ )
   {
      to_list.push_back((const char*)(*to_it).GetMimeStr().mb_str(wxConvLocal));
   }
   me.header().to(to_list);

   AddressList cc_list;
   std::list<Address>::iterator cc_it;
   for ( cc_it=m_ccArray.begin() ; cc_it != m_ccArray.end(); cc_it++ )
   {
      cc_list.push_back((const char*)(*cc_it).GetMimeStr().mb_str(wxConvLocal));
   }
   me.header().cc(cc_list);

   me.header().subject((const char*)m_subject.mb_str(wxConvLocal));

   /* Determine the parent element type */
   MimeEntity* root;
   MimeEntity* attachments_parent = NULL;
   if (attachments_names.empty() && !has_html_alternative)
   {
      /* We only need a TextPlain Element */
      root = new TextPlain((const char*)m_text.mb_str(wxConvLocal));
   }
   else if (attachments_names.empty())
   {
      /* We will generate an alternate part with text and html content */
      root = new MultipartAlternative();

      /* Insert text data */
      TextPlain* content = new TextPlain((const char*)m_text.mb_str(wxConvLocal));
      root->body().parts().insert(root->body().parts().end(),
                                  content);

      /* Insert HTML data */
      //TODO : I have no idea on how to do so...
      TextPlain* html = new TextPlain((const char*)html_alternative.mb_str(wxConvLocal));
      html->header().contentType(ContentType("text","html"));
      root->body().parts().insert(root->body().parts().end(),
                                  html);
   }
   else if (!has_html_alternative)
   {
      root = new MultipartMixed();
      attachments_parent = root;

      /* Insert text data */
      TextPlain* content = new TextPlain((const char*)m_text.mb_str(wxConvLocal));
      root->body().parts().insert(root->body().parts().end(),
                                  content);

   }
   else
   {
      root = new MultipartMixed();
      attachments_parent = root;

      /* We will generate an alternate part with text and html content */
      MultipartAlternative* alt = new MultipartAlternative();
      root->body().parts().insert(root->body().parts().end(),
                                  alt);

      /* Insert text data */
      TextPlain* content = new TextPlain((const char*)m_text.mb_str(wxConvLocal));
      alt->body().parts().insert(alt->body().parts().end(),
                                 content);

      /* Insert HTML data */
      //TODO : I have no idea on how to do so...
      TextPlain* html = new TextPlain((const char*)html_alternative.mb_str(wxConvLocal));
      html->header().contentType(ContentType("text","html"));
      alt->body().parts().insert(alt->body().parts().end(),
                                 html);
   }

   /* Add all attachments */
   std::vector<wxString>::iterator attachments_names_it;
   std::vector<std::vector<unsigned char> >::iterator attachments_contents_it;
   for ( attachments_names_it=attachments_names.begin(), attachments_contents_it=attachments_contents.begin();
            attachments_names_it != attachments_names.end();
            attachments_names_it++,attachments_contents_it++)
   {
      Attachment* at = new Attachment((const char*)(*attachments_names_it).mb_str(wxConvLocal),
                                      *attachments_contents_it);
      attachments_parent->body().parts().insert(attachments_parent->body().parts().end(),
                                                at);
   }

   /* Assign the root content to message body */
   //TODO : I should find a better way to do this stuff...
   std::stringstream root_content(std::stringstream::out);
   root_content << *root;

   me.body() = root_content.str();

   /* delete root */
   delete root;

   /* Generate the message content */
   std::stringstream result(std::stringstream::out);
   result << me;

   /* Convert . stuff */
   wxString wx_result(result.str().c_str(), wxConvLocal);
   wx_result.Replace(_T("\x00a."), _T("\x00a.."), true);
   if (wx_result.StartsWith(_T(".")))
   {
      wx_result.insert(0, _T("."));
   }

   /* Append final . */
   wx_result << _T("\x00d\x00a.\x00d\x00a");
   
   /* Send it to server */
   out.Write((const char*)wx_result.mb_str(wxConvLocal),  wx_result.length());
}
