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
 $Log: wxstates.cpp,v $
 Revision 1.5  2004/06/29 11:06:20  tavasti
 - Added OnResponse handler also for initial state (sometimes OnConnect
   arrives after first data)
 - Minor changes in indentation & comments

 Revision 1.4  2003/11/21 12:36:46  tavasti
 - Makefilet -Wall optioilla
 - Korjattu 'j‰rkev‰t' varoitukset pois (J‰‰nyt muutama joita ei saa
   kohtuudella poistettua)

 Revision 1.3  2003/11/13 17:12:15  tavasti
 - Muutettu tiedostojen nimet wx-alkuisiksi

 Revision 1.2  2003/11/07 09:17:40  tavasti
 - K‰‰ntyv‰ versio, fileheaderit lis‰tty.


****************************************************************************/

//static char cvs_id[] = "$Header: /v/CVS/olive/notifier/wxSMTP/src/wxstates.cpp,v 1.3 2004/09/09 20:38:51 paul Exp $";

/*
 * Purpose: private wxWindows helper classes for SMTP
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

#include <wx/sckstrm.h>

#include <3rdparty/email/smtp/wxsmtp.h>

void wxSMTP::ConnectState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering ConnectState\n");

   /* Set disconnection status */
   ((wxSMTP&)context).disconnection_status = Listener::StatusOK;

   /* Set ssl flag */
   ((wxSMTP&)context).shall_enter_ssl = ((wxSMTP&)context).ssl_enabled;

   /* Start connection process */
   context.Connect();

   /* Start the timer */
   context.TimerStart(((wxSMTP&)context).timeout);
}

void wxSMTP::ConnectState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving ConnectState\n");
   context.TimerStop();
}

void wxSMTP::ConnectState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* Check if server ACK */
   if (smtpCode == 220)
   {
      context.ChangeState(g_heloState);
   }
   else
   {
      if ((smtpCode >= 400) && (smtpCode < 500))
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusRetry;
      }
      else
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusError;
      }
      context.ChangeState(g_quitState);
   }
}

void wxSMTP::ConnectState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusDisconnect;
   context.ChangeState(g_closedState);
}

void wxSMTP::ConnectState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusTimeout;
   context.ChangeState(g_quitState);
}

void wxSMTP::ConnectState::onFlushMessages(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).messages_to_send.clear();
}

void wxSMTP::HeloState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering HeloState\n");

   /* Send the HELLO command */
   //TODO replace localhost with name of machine
   context.SendLine(_T("EHLO localhost"));

   /* Start timer */
   context.TimerStart(((wxSMTP&)context).timeout);
}

void wxSMTP::HeloState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving HeloState\n");
   context.TimerStop();
}

void wxSMTP::HeloState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* Check if command was successful */
   if (smtpCode == 250)
   {
      /* Check if this is the last answer */
      if (line.StartsWith(wxT("250-")))
      {
		  if (line.StartsWith(wxT("250-AUTH"))) {
				((wxSMTP&)context).authentication_line = line;
		  }
         /* We shall wait next acceptance answer... */

      }
      else
      {
         if (((wxSMTP&)context).shall_enter_ssl)
         {
            context.ChangeState(g_startTlsState);
         }
         else
         {
            if (((wxSMTP&)context).authentication_scheme == wxSMTP::NoAuthentication)
            {
               context.ChangeState(g_sendMailFromState);
            }
            else
            {
               context.ChangeState(g_authenticateState);
            }
         }
      }
   }
   else
   {
      if ((smtpCode >= 400) && (smtpCode < 500))
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusRetry;
      }
      else
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusError;
      }
      context.ChangeState(g_quitState);
   }
}

void wxSMTP::HeloState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusDisconnect;
   context.ChangeState(g_closedState);
}

void wxSMTP::HeloState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusTimeout;
   context.ChangeState(g_quitState);
}

void wxSMTP::HeloState::onFlushMessages(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).messages_to_send.clear();
}

void wxSMTP::StartTLSState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering StartTLSState\n");

   /* Send the HELLO command */
   context.SendLine(_T("STARTTLS"));

   /* Start timer */
   context.TimerStart(((wxSMTP&)context).timeout);
}

void wxSMTP::StartTLSState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving StartTLSState\n");
   context.TimerStop();
}

void wxSMTP::StartTLSState::onFlushMessages(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).messages_to_send.clear();
}

void wxSMTP::StartTLSState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* Check if command was successful */
   if (smtpCode == 220)
   {
      context.ChangeState(g_sslNegociationState);
   }
   else
   {
      if ((smtpCode >= 400) && (smtpCode < 500))
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusRetry;
      }
      else
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusError;
      }
      context.ChangeState(g_quitState);
   }
}

void wxSMTP::StartTLSState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusDisconnect;
   context.ChangeState(g_closedState);
}

void wxSMTP::StartTLSState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusTimeout;
   context.ChangeState(g_quitState);
}

void wxSMTP::SSLNegociationState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering SSLNegociationState\n");

   /* Send the HELLO command */
   switch (context.InitiateSSLSession())
   {
      case SslConnected:
         context.ChangeState(g_heloState);
         break;

      case SslPending:
         context.TimerStart(((wxSMTP&)context).timeout);
         break;

      case SslFailed:
      default:
         ((wxSMTP&)context).disconnection_status = Listener::StatusSslError;
         context.ChangeState(g_quitState);
         break;
   }
}

void wxSMTP::SSLNegociationState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving SSLNegociationState\n");
   context.TimerStop();
   ((wxSMTP&)context).shall_enter_ssl = false;
}

void wxSMTP::SSLNegociationState::onResponse(wxCmdlineProtocol& context, const wxString& WXUNUSED(line)) const
{
   /* try a new connection */
   switch (context.InitiateSSLSession())
   {
      case SslConnected:
         context.ChangeState(g_heloState);
         break;

      case SslPending:
         /* We will wait to have more data */
         break;

      case SslFailed:
      default:
         ((wxSMTP&)context).disconnection_status = Listener::StatusSslError;
         context.ChangeState(g_quitState);
         break;
   }
}

void wxSMTP::SSLNegociationState::onFlushMessages(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).messages_to_send.clear();
}

void wxSMTP::SSLNegociationState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusDisconnect;
   context.ChangeState(g_closedState);
}

void wxSMTP::SSLNegociationState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusTimeout;
   context.ChangeState(g_quitState);
}

void wxSMTP::AuthenticateState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering AuthenticateState\n");

   if (((wxSMTP&)context).authentication_scheme == wxSMTP::AutodetectAuthenticationMethod) {

	   if (((wxSMTP&)context).authentication_line.Find(wxT("LOGIN"))!=wxNOT_FOUND) {
			((wxSMTP&)context).current_authentication_scheme = wxSMTP::LoginAuthentication;

	   } else if (((wxSMTP&)context).authentication_line.Find(wxT("CRAM_MD5"))!=wxNOT_FOUND) {
			((wxSMTP&)context).current_authentication_scheme = wxSMTP::CramMd5Authentication;

	   } else if (((wxSMTP&)context).authentication_line.Find(wxT("PLAIN"))!=wxNOT_FOUND) {
			((wxSMTP&)context).current_authentication_scheme = wxSMTP::PlainAuthentication;

	   } else ((wxSMTP&)context).current_authentication_scheme = wxSMTP::LoginAuthentication;

   } else ((wxSMTP&)context).current_authentication_scheme = ((wxSMTP&)context).authentication_scheme;


   switch (((wxSMTP&)context).current_authentication_scheme)
   {
	case wxSMTP::LoginAuthentication:
       context.SendLine(_T("AUTH LOGIN"));
	   break;
	case wxSMTP::CramMd5Authentication:
	   context.SendLine(_T("AUTHENTICATE CRAM-MD5"));
		break;
	case wxSMTP::PlainAuthentication:
       context.SendLine(_T("AUTH PLAIN"));
		break;
   }
   /* check authentication type */

   /* Initialize  internal state */
   ((wxSMTP&)context).authentication_digest_sent = false;

   /* Start timer */
   context.TimerStart(((wxSMTP&)context).timeout);
}

void wxSMTP::AuthenticateState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving AuthenticateState\n");
   context.TimerStop();
}

void wxSMTP::AuthenticateState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* Check if we alreadysent authentication scheme */
   if (((wxSMTP&)context).authentication_digest_sent)
   {
      /* Check if server acknowledged connection */
      if (smtpCode == 235)
      {
         context.ChangeState(g_sendMailFromState);
      }
      else if (smtpCode == 535)
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusInvalidUserNamePassword;
         context.ChangeState(g_quitState);
      }
      else
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusError;
         context.ChangeState(g_quitState);
      }
   }
   else
   {
      /* check is server accepted command */
      if (smtpCode == 334)
      {
         /* Extract digest and send answer */
         context.SendLine(((wxSMTP&)context).ComputeAuthenticationDigest(line.AfterFirst(' ')));
      }
      else
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusError;
         context.ChangeState(g_quitState);
      }
   }
}

void wxSMTP::AuthenticateState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusDisconnect;
   context.ChangeState(g_closedState);
}

void wxSMTP::AuthenticateState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusTimeout;
   context.ChangeState(g_quitState);
}

void wxSMTP::AuthenticateState::onFlushMessages(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).messages_to_send.clear();
}

void wxSMTP::SendMailFromState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering SendMailFromState\n");

   /* First, parse complete retry list to see if some messages expired */
   wxDateTime current_time = wxDateTime::Now();
   while ((((wxSMTP&)context).retry_messages_list.size() > 0) &&
         (((wxSMTP&)context).retry_messages_list.front().retry_time <= current_time))
   {
      if (!((wxSMTP&)context).retry_messages_list.front().is_reconnection)
      {
         ((wxSMTP&)context).messages_to_send.push_back(((wxSMTP&)context).retry_messages_list.front().message);
      }
      ((wxSMTP&)context).retry_messages_list.pop_front();
   }

   /* Then, we check if we have a message to send */
   if (((wxSMTP&)context).messages_to_send.size() <= 0)
   {
      context.ChangeState(g_quitState);
   }
   else
   {
      /* Check if we shall clear the lists */
      if (!((wxSMTP&)context).messages_to_send.front().RecipientsAlreadySent())
      {
         ((wxSMTP&)context).total_accepted_recipients.clear();
         ((wxSMTP&)context).total_rejected_recipients.clear();
      }

      /* Send the FROM command */
      context.SendLine(_T("MAIL FROM: <") +
                       ((wxSMTP&)context).messages_to_send.front().GetFrom().GetAddress() + _T(">"));
      context.TimerStart(((wxSMTP&)context).timeout);
   }
}

void wxSMTP::SendMailFromState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving SendMailFromState\n");
   context.TimerStop();
}

void wxSMTP::SendMailFromState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* Check if command was successful */
   if (smtpCode == 250)
   {
      context.ChangeState(g_rcptListState);
   }
   else
   {
      if ((smtpCode >= 400) && (smtpCode < 500))
      {
         ((wxSMTP&)context).disconnection_status = Listener::StatusRetry;
         context.ChangeState(g_quitState);
      }
      else
      {
         /* Message has been rejected -> trigger callback */
         bool shall_retry;
         bool shall_stop;
         unsigned long retry_delay;
         ((wxSMTP&)context).m_pListener->OnMessageStatus(((wxSMTP&)context).messages_to_send.front().GetMessageId(),
                                                         Listener::SendingMessageRejected,
                                                         ((wxSMTP&)context).messages_to_send.size()-1,
                                                         ((wxSMTP&)context).GetNbRetryMessages(),
                                                         std::list<wxEmailMessage::Address>(),
                                                         std::list<wxEmailMessage::Address>(),
                                                         true,
                                                         shall_retry,
                                                         retry_delay,
                                                         shall_stop);

         /* Check if we shall retry sending process */
         if (shall_retry)
         {
            /* Format retry element */
            RetryInfo_t retry_info;
            retry_info.message = ((wxSMTP&)context).messages_to_send.front();
            retry_info.is_reconnection = false;
            retry_info.retry_time = wxDateTime::Now() + wxTimeSpan::Seconds(retry_delay);

            /* Search place where it will be inserted */
            std::list<RetryInfo_t>::iterator it = ((wxSMTP&)context).retry_messages_list.begin();
            while ((it != ((wxSMTP&)context).retry_messages_list.end()) &&
                   (it->retry_time < retry_info.retry_time))
            {
               it++;
            }
            ((wxSMTP&)context).retry_messages_list.insert(it, retry_info);
         }

         /* Remove it from messages list */
         ((wxSMTP&)context).messages_to_send.pop_front();

         /* Check if we shall stop */
         if (shall_stop)
         {
            ((wxSMTP&)context).disconnection_status = Listener::StatusUserAbort;
            context.ChangeState(g_quitState);
         }
         else
         {
            context.ChangeState(g_sendMailFromState);
         }
      }
   }
}

void wxSMTP::SendMailFromState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusTimeout;
   context.ChangeState(g_quitState);
}

void wxSMTP::SendMailFromState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusDisconnect;
   context.ChangeState(g_closedState);
}

void wxSMTP::SendMailFromState::onFlushMessages(wxCmdlineProtocol& context) const
{
   /* We cannot flush the current message as it is currently handled */
   while (((wxSMTP&)context).messages_to_send.size() > 1)
   {
      ((wxSMTP&)context).messages_to_send.pop_back();
   }
}

void wxSMTP::CancelMailSendingProcess(Listener::SendingStatus_t status, bool accept_retry, bool shall_quit, bool shall_send_quit, Listener::DisconnectionStatus_t disconnection_status)
{
   /* Generate the callback with proper status */
   bool shall_retry;
   unsigned long retry_delay;
   bool shall_stop;
   m_pListener->OnMessageStatus(messages_to_send.front().GetMessageId(),
                                status,
                                messages_to_send.size()-1,
                                GetNbRetryMessages(),
                                total_rejected_recipients,
                                total_accepted_recipients,
                                accept_retry,
                                shall_retry,
                                retry_delay,
                                shall_stop);

   /* Check if we shall retry */
   if (accept_retry)
   {
      if (shall_retry)
      {
         /* Reinsert all pending recipients */
         std::list<wxEmailMessage::Address>::const_iterator it;
         for (it = pending_accepted_recipients.begin();
                  it != pending_accepted_recipients.end();
                     it++)
         {
            messages_to_send.front().ReinsertRecipient(*it);
         }

         /* Format retry element */
         RetryInfo_t retry_info;
         retry_info.message = messages_to_send.front();
         retry_info.is_reconnection = false;
         retry_info.retry_time = wxDateTime::Now() + wxTimeSpan::Seconds(retry_delay);

         /* Search place where it will be inserted */
         std::list<RetryInfo_t>::iterator it2 = retry_messages_list.begin();
         while ((it2 != retry_messages_list.end()) &&
                (it2->retry_time < retry_info.retry_time))
         {
            it2++;
         }
         retry_messages_list.insert(it2, retry_info);
      }
   }
   else
   {
      wxASSERT(!shall_retry);
   }

   /* remove message from list */
   messages_to_send.pop_front();

   /* Check if we shall force a quit */
   if (shall_quit)
   {
      /* Assign disconnection status */
      disconnection_status = disconnection_status;

      /* check if we shall send the quit command */
      if (shall_send_quit)
      {
         ChangeState(g_quitState);
      }
      else
      {
         ChangeState(g_closedState);
      }
   }
   else
   {
      /* check if user requested a stop */
      if (shall_stop)
      {
         disconnection_status = Listener::StatusUserAbort;
         ChangeState(g_quitState);
      }
      else
      {
         ChangeState(g_sendMailFromState);
      }
   }
}

void wxSMTP::RcptListState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering RcptListState\n");

   /* Reset all internal stuff */
   ((wxSMTP&)context).nb_pending_recipients = 0;
   ((wxSMTP&)context).pending_accepted_recipients.clear();

   /* check if there is at least one recipient */
   if (!((wxSMTP&)context).messages_to_send.front().GetNextRecipient(((wxSMTP&)context).current_recipient))
   {
      ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingMessageRejected, false, false, false, (Listener::DisconnectionStatus_t)0);
   }
   else
   {
      /* Send Rcpt command */
      context.SendLine(_T("RCPT TO: <") + ((wxSMTP&)context).current_recipient.GetAddress() + _T(">"));
   }

   /* Start timer */
   context.TimerStart(((wxSMTP&)context).timeout);
}

void wxSMTP::RcptListState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* Check if command was successful */
   if ((smtpCode >= 200) &&
       (smtpCode < 300))
   {
      /* Reset timer */
      context.TimerRestart();

      /* update recipients status */
      ((wxSMTP&)context).nb_pending_recipients++;
      ((wxSMTP&)context).pending_accepted_recipients.emplace_back(((wxSMTP&)context).current_recipient);

      /* Check if there are still recipients to send */
      if (((wxSMTP&)context).messages_to_send.front().GetNextRecipient(((wxSMTP&)context).current_recipient))
      {
         /* Send Rcpt command */
         context.SendLine(_T("RCPT TO: <") + ((wxSMTP&)context).current_recipient.GetAddress() + _T(">"));
      }
      else
      {
         context.ChangeState(g_beginDataState);
      }
   }
   else if (smtpCode == 552)
   {
      /* We probably reached nb recipients limit -> send this mail */
      ((wxSMTP&)context).messages_to_send.front().ReinsertRecipient(((wxSMTP&)context).current_recipient);
      context.ChangeState(g_beginDataState);
   }
   else if ((smtpCode == 550) ||
            (smtpCode == 551) ||
            (smtpCode == 553) ||
            (smtpCode == 555))
   {
      /* Probably an invalid address */
      ((wxSMTP&)context).total_rejected_recipients.emplace_front(((wxSMTP&)context).current_recipient);

      /* Try sending next recipient */
      if (((wxSMTP&)context).messages_to_send.front().GetNextRecipient(((wxSMTP&)context).current_recipient))
      {
         /* Send Rcpt command */
         context.SendLine(_T("RCPT TO: <") + ((wxSMTP&)context).current_recipient.GetAddress() + _T(">"));
      }
      else
      {
         /* Check if we have at least one recipient */
         if (((wxSMTP&)context).nb_pending_recipients > 0)
         {
            /* Switch to begin state */
            context.ChangeState(g_beginDataState);
         }
         else
         {
            /* Cancel current sending */
            if (((wxSMTP&)context).nb_recipients_successful > 0)
            {
               ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingSucceeded, false, false, false, (Listener::DisconnectionStatus_t)0);
            }
            else
            {
               ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingNoValidRecipient, false, false, false, (Listener::DisconnectionStatus_t)0);
            }
         }
      }
   }
   else
   {
      ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingNoValidRecipient, true, false, false, (Listener::DisconnectionStatus_t)0);
   }
}

void wxSMTP::RcptListState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving RcptListState\n");
   context.TimerStop();
}

void wxSMTP::RcptListState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingTimeout, true, true, true, Listener::StatusTimeout);
}

void wxSMTP::RcptListState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingDisconected, true, true, true, Listener::StatusDisconnect);
}

void wxSMTP::RcptListState::onFlushMessages(wxCmdlineProtocol& context) const
{
   /* We cannot flush the current message as it is currently handled */
   while (((wxSMTP&)context).messages_to_send.size() > 1)
   {
      ((wxSMTP&)context).messages_to_send.pop_back();
   }
}

void wxSMTP::BeginDataState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering BeginDataState\n");

   /* Send DATA command */
   context.SendLine(_T("DATA"));

   /* Start timer */
   context.TimerStart(((wxSMTP&)context).timeout);
}

void wxSMTP::BeginDataState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving BeginDataState\n");

   /* Stop timer */
   context.TimerStop();
}

void wxSMTP::BeginDataState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* check response type */
   if (smtpCode == 354)
   {
      /* Switch to Data state */
      context.ChangeState(g_dataState);
   }
   else if ((smtpCode >= 400) && (smtpCode < 500))
   {
      /* Abort message with timeout */
      ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingRetry, true, false, false, (Listener::DisconnectionStatus_t)0);
   }
   else
   {
      /* Abort message with timeout */
      ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingError, true, false, false, (Listener::DisconnectionStatus_t)0);
   }
}

void wxSMTP::BeginDataState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingError, true, true, false, Listener::StatusDisconnect);
}

void wxSMTP::BeginDataState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingTimeout, true, true, true, Listener::StatusTimeout);
}

void wxSMTP::BeginDataState::onFlushMessages(wxCmdlineProtocol& context) const
{
   /* We cannot flush the current message as it is currently handled */
   while (((wxSMTP&)context).messages_to_send.size() > 1)
   {
      ((wxSMTP&)context).messages_to_send.pop_back();
   }
}

void wxSMTP::DataState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering DataState\n");

   /* Send the complete message on socket */
   wxSslSocketOutputStream out(context);
   ((wxSMTP&)context).messages_to_send.front().Encode(out);

   /* Start timer */
   context.TimerStart(((wxSMTP&)context).timeout);
}

void wxSMTP::DataState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving DataState\n");

   /* Stop the timer */
   context.TimerStop();
}

void wxSMTP::DataState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* Check if we received an ACK */
   if (smtpCode == 250)
   {
      /* flush all pending recipients */
      while (((wxSMTP&)context).pending_accepted_recipients.size() > 0)
      {
         ((wxSMTP&)context).total_accepted_recipients.emplace_back(((wxSMTP&)context).pending_accepted_recipients.front());
         ((wxSMTP&)context).pending_accepted_recipients.pop_front();
      }

      /* Check if message has still recipients */
      if (((wxSMTP&)context).messages_to_send.front().HasStillRecipients())
      {
         /* jump to state send mail from */
         context.ChangeState(g_sendMailFromState);
      }
      else
      {
         /* Generate the callback */
         bool shall_retry;
         unsigned long retry_delay;
         bool shall_stop;
         ((wxSMTP&)context).m_pListener->OnMessageStatus(((wxSMTP&)context).messages_to_send.front().GetMessageId(),
                                                         Listener::SendingSucceeded,
                                                         ((wxSMTP&)context).messages_to_send.size()-1,
                                                         ((wxSMTP&)context).GetNbRetryMessages(),
                                                         ((wxSMTP&)context).total_rejected_recipients,
                                                         ((wxSMTP&)context).total_accepted_recipients,
                                                         false,
                                                         shall_retry,
                                                         retry_delay,
                                                         shall_stop);

         /* Remove mail from list */
         ((wxSMTP&)context).messages_to_send.pop_front();

         /* we cannot retry.. */
         wxASSERT(!shall_retry);

         /* Check if we shall leave */
         if (shall_stop)
         {
            ((wxSMTP&)context).disconnection_status = Listener::StatusUserAbort;
            context.ChangeState(g_quitState);
         }
         else
         {
            context.ChangeState(g_sendMailFromState);
         }
      }
   }
   else if ((smtpCode >= 400) &&
            (smtpCode < 500))
   {
      ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingRetry, true, false, false, (Listener::DisconnectionStatus_t)0);
   }
   else
   {
      ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingError, true, false, false, (Listener::DisconnectionStatus_t)0);
   }
}

void wxSMTP::DataState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingDisconected, true, true, false, Listener::StatusDisconnect);
}

void wxSMTP::DataState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).CancelMailSendingProcess(Listener::SendingTimeout, true, true, true, Listener::StatusTimeout);
}

void wxSMTP::DataState::onFlushMessages(wxCmdlineProtocol& context) const
{
   /* We cannot flush the current message as it is currently handled */
   while (((wxSMTP&)context).messages_to_send.size() > 1)
   {
      ((wxSMTP&)context).messages_to_send.pop_back();
   }
}

void wxSMTP::QuitState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering QuitState\n");

   /* Send quit command */
   context.SendLine(_T("QUIT"));

   /* Start timer */
   context.TimerStart(((wxSMTP&)context).timeout);
}

void wxSMTP::QuitState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving QuitState\n");
   context.TimerStop();
}

void wxSMTP::QuitState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
   /* Extract smpt code */
   unsigned long smtpCode = 0;
   line.ToULong(&smtpCode);

   /* Check if we received an ACK */
   if (smtpCode != 221)
   {
      ((wxSMTP&)context).disconnection_status = Listener::StatusError;
   }

   /* Go to exit state */
   context.ChangeState(g_closedState);
}

void wxSMTP::QuitState::onTimeout(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusTimeout;
   context.ChangeState(g_closedState);
}

void wxSMTP::QuitState::onDisconnect(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).disconnection_status = Listener::StatusDisconnect;
   context.ChangeState(g_closedState);
}

void wxSMTP::QuitState::onFlushMessages(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).messages_to_send.clear();
}

void wxSMTP::QuitState::onStartSendingMails(wxCmdlineProtocol& context) const
{
   /* We shall post an event to restart connection immediately after disconnection */
   RetryInfo_t retry_info;
   retry_info.retry_time = wxDateTime::Now();
   retry_info.is_reconnection = true;
   /* No need to sort this entry as it will be immediately popped... */
   ((wxSMTP&)context).retry_messages_list.push_front(retry_info);
}

void wxSMTP::ClosedState::onEnterState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Entering ClosedState\n");

   /* Disconnect from host */
   context.Disconnect();

   /* Check if we shall trigger callback */
   if (((wxSMTP&)context).shall_trigger_disconnection_callback)
   {
      /* Trigger the callback */
      bool shall_retry = false;
      unsigned long retry_delay;
      ((wxSMTP&)context).m_pListener->OnDisconnect(((wxSMTP&)context).disconnection_status,
                                                   ((wxSMTP&)context).messages_to_send.size(),
                                                   ((wxSMTP&)context).GetNbRetryMessages(),
                                                   shall_retry,
                                                   retry_delay);

      /* Post event, if any */
      if (shall_retry)
      {
         /* Format retry element */
          RetryInfo_t retry_info;
          retry_info.is_reconnection = true;
          retry_info.retry_time = wxDateTime::Now() + wxTimeSpan::Seconds(retry_delay);

          /* Search place where it will be inserted */
          std::list<RetryInfo_t>::iterator it = ((wxSMTP&)context).retry_messages_list.begin();
          while ((it != ((wxSMTP&)context).retry_messages_list.end()) &&
                 (it->retry_time < retry_info.retry_time))
          {
             it++;
          }
          ((wxSMTP&)context).retry_messages_list.insert(it, retry_info);
      }
   }
   else
   {
      /* Set the flag */
      ((wxSMTP&)context).shall_trigger_disconnection_callback = true;
   }

   /* parse complete retry list to check if we shall perform something now */
   bool shall_reconnect = false;
   wxDateTime current_time = wxDateTime::Now();
   while ((((wxSMTP&)context).retry_messages_list.size() > 0) &&
          (((wxSMTP&)context).retry_messages_list.front().retry_time <= current_time))
   {
      if (!((wxSMTP&)context).retry_messages_list.front().is_reconnection)
      {
         ((wxSMTP&)context).messages_to_send.push_back(((wxSMTP&)context).retry_messages_list.front().message);
      }
      shall_reconnect = true;
      ((wxSMTP&)context).retry_messages_list.pop_front();
   }

   /* Check if we shall reconnect now */
   if (shall_reconnect)
   {
      context.ChangeState(g_connectState);
   }
   else
   {
      /* Check if we are supposed to insert a timer to leace this state */
      if (((wxSMTP&)context).retry_messages_list.size() > 0)
      {
         /* Compute next event time */
         wxTimeSpan delay = ((wxSMTP&)context).retry_messages_list.front().retry_time - current_time;
         long long delay_seconds = delay.GetSeconds().GetValue();
         if (delay_seconds <= 0) delay_seconds = 1;

         /* Initialize  timer */
         context.TimerStart(delay_seconds);
      }
   }
}

void wxSMTP::ClosedState::onLeaveState(wxCmdlineProtocol& context) const
{
   WX_SMTP_PRINT_DEBUG("Leaving ClosedState\n");
   context.TimerStop();
}

void wxSMTP::ClosedState::onTimeout(wxCmdlineProtocol& context) const
{
   /* pop every event from retry list that have expired */
   wxDateTime current_time = wxDateTime::Now();
   while ((((wxSMTP&)context).retry_messages_list.size() > 0) &&
          (((wxSMTP&)context).retry_messages_list.front().retry_time <= current_time))
   {
      if (!((wxSMTP&)context).retry_messages_list.front().is_reconnection)
      {
         ((wxSMTP&)context).messages_to_send.push_back(((wxSMTP&)context).retry_messages_list.front().message);
      }
      ((wxSMTP&)context).retry_messages_list.pop_front();
   }

   /* Perform reconnection */
   context.ChangeState(g_connectState);
}

void wxSMTP::ClosedState::onStartSendingMails(wxCmdlineProtocol& context) const
{
   /* Change the state */
   context.ChangeState(g_connectState);
}

void wxSMTP::ClosedState::onFlushMessages(wxCmdlineProtocol& context) const
{
   ((wxSMTP&)context).messages_to_send.clear();
}
