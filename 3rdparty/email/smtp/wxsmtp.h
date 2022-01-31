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
 $Log: wxsmtp.h,v $
 Revision 1.4  2004/06/29 11:06:20  tavasti
 - Added OnResponse handler also for initial state (sometimes OnConnect
   arrives after first data)
 - Minor changes in indentation & comments

 Revision 1.3  2003/11/13 17:12:15  tavasti
 - Muutettu tiedostojen nimet wx-alkuisiksi

 Revision 1.2  2003/11/07 09:17:40  tavasti
 - K‰‰ntyv‰ versio, fileheaderit lis‰tty.


****************************************************************************/

/*
 * Purpose: private wxWindows mail transport implementation
 * Author:  Frank Buﬂ
 * Created: 2002
 */

#ifndef _WX_SMPT_H
#define _WX_SMPT_H

#include <wx/wx.h>
#include <wx/protocol/protocol.h>

#include "../wxsmtpdef.h"

#include "../utils/wxcmdprot.h"

/*!
 * This class implements a SMTP client, used for sending e-mails.
 *
 * \section intro_sec Introduction
 *
 * In order to use this class, you should instanciate this class once for each
 * SMTP server you want to communicate with. Once th class is instanciated, you
 *
 * The e-mails that shall be sent are represented by the ::wxSmtpEmailMessage
 * class.
 *
 * \if coders
 *
 * \section impl_sec Imlementation details
 *
 * This class uses the ::wxCmdlineProtocol class for implementation. The following graph represents
 * the implemented state machine :
 *
 * \dot
 * digraph LauncherFSM {
 *
 *    node [shape=record, fontname=Helvetica, fontsize=10];
 *
 *    ConnectState [ label="ConnectState" URL="\ref wxSMTP::ConnectState"];
 *    HeloState [ label="HeloState" URL="\ref wxSMTP::HeloState"];
 *    StartTLSState [ label="StartTLSState" URL="\ref wxSMTP::StartTLSState"];
 *    SSLNegociationState [ label="SSLNegociationState" URL="\ref wxSMTP::SSLNegociationState"];
 *    AuthenticateState [ label="AuthenticateState" URL="\ref wxSMTP::AuthenticateState"];
 *    SendMailFromState [ label="SendMailFromState" URL="\ref wxSMTP::SendMailFromState"];
 *    RcptListState [ label="RcptListState" URL="\ref wxSMTP::RcptListState"];
 *    BeginDataState [ label="BeginDataState" URL="\ref wxSMTP::BeginDataState"];
 *    DataState [ label="DataState" URL="\ref wxSMTP::DataState"];
 *    QuitState [ label="QuitState" URL="\ref wxSMTP::QuitState"];
 *    ClosedState [ label="ClosedState" URL="\ref wxSMTP::ClosedState"];
 *
 *    ConnectState -> HeloState [ label="OK", arrowhead="open", style="dashed" ];
 *    ConnectState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ];
 *    ConnectState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ];
 *
 *    HeloState -> StartTLSState [ label="OK,SSL", arrowhead="open", style="dashed" ];
 *    HeloState -> SendMailFromState [ label="OK,NoAuth,NoSSL", arrowhead="open", style="dashed" ];
 *    HeloState -> AuthenticateState [ label="OK,ShallAuth,NoSSL", arrowhead="open", style="dashed" ];
 *    HeloState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ];
 *    HeloState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ];
 *
 *    StartTLSState -> SSLNegociationState [ label="OK", arrowhead="open", style="dashed" ];
 *    StartTLSState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ];
 *    StartTLSState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ];
 *
 *    SSLNegociationState -> HeloState [ label="OK", arrowhead="open", style="dashed" ];
 *    SSLNegociationState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ];
 *    SSLNegociationState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ];
 *
 *    AuthenticateState -> SendMailFromState [ label="OK", arrowhead="open", style="dashed" ]
 *    AuthenticateState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ]
 *    AuthenticateState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ]
 *
 *    SendMailFromState -> RcptListState [ label="OK", arrowhead="open", style="dashed" ];
 *    SendMailFromState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ];
 *    SendMailFromState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ];
 *    SendMailFromState -> QuitState [ label="NoMsg", arrowhead="open", style="dashed" ];
 *
 *    RcptListState -> BeginDataState [ label="OK", arrowhead="open", style="dashed" ];
 *    RcptListState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ];
 *    RcptListState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ];
 *
 *    BeginDataState -> DataState [ label="OK", arrowhead="open", style="dashed" ];
 *    BeginDataState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ];
 *    BeginDataState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ];
 *
 *    DataState -> QuitState [ label="OK", arrowhead="open", style="dashed" ];
 *    DataState -> QuitState [ label="KO|timeout", arrowhead="open", style="dashed" ];
 *    DataState -> ClosedState [ label="disconnect", arrowhead="open", style="dashed" ];
 *
 *    QuitState -> ClosedState [ label="OK|KO|timeout|\ndisconnect", arrowhead="open", style="dashed" ];
 *
 *    ClosedState -> ConnectState [ label="RetryTimer|userPostMessage", arrowhead="open", style="dashed" ];
 *
 * \enddot
 *
 * \endif
 */
class WXDLLIMPEXP_SMTP wxSMTP : public wxCmdlineProtocol
{
   public:

      /**
       * This class allows being informed of the status of the sending of an e-mail.
       *
       * As the sending process is asynchrounous, to be notified of the sending status,
       * the caller shall derive a class from this one and implement its own
       * ::wxSMTP::Listener::OnMessageStatus method.
       *
       * The class can then be registered for notifications.
       *
       * \see For an example see SendMail Sample.
       */
      class WXDLLIMPEXP_SMTP Listener
      {
         public:

            /*!
             * This defines all possible status of mail sending process.
             */
            typedef enum
            {
                SendingSucceeded = 0,     /*!< The mail has been properly sent to the server. */
                SendingTimeout,           /*!< A timeout condition occurred while sending the mail */
                SendingDisconected,       /*!< The connection was lost while sending the mail */
                SendingMessageRejected,   /*!< The message is rejected due to an invalid content */
                SendingNoValidRecipient,  /*!< The message has no valid recipient */
                SendingRetry,             /*!< The sending process has been temporarily rejected by the server.*/
                SendingError              /*!< An unknown error occurred while sending the mail */
            } SendingStatus_t;

            /*!
             * This function is invoked when a message has been sent to the server.
             *
             * \param message_id the message id to which this callback refers
             * \param status The ::SendingStatus_t of the messahe
             * \param nb_pending_messages the number of messages that shall still be sent (not including the ones that are delayed for retry)
             * \param nb_retry_messages the number of messages that are delayed for retry
             * \param rejected_addresses the list of all addresses that were rejected. In case of retry, those addresses will not be tried anymore.
             * \param accepted_addresses The list of all addresses that were properly handled. Note that even
             *        if the status is not ::wxSMTP::Listener::SendingSucceeded, some recipients could be properly handled.
             *        In this case, the caller can make the assumption that those recipients have properly
             *        been handled and those addresses will not be included in retry process, if any.
             * \param can_retry if true, user can request a retry, if false, message is definitively rejected and retry is not possible.
             * \param shall_retry If status is different from ::wxSMTP::Listener::SendingSucceeded, the user shall set this
             *        flag to indicate if the SMTP client shall retry sending the mail. Note that this is
             *        not a good idea to set this flag if status is ::SendingMessageRejected.
             * \param retry_delay_s If the user selects a retry, the delay before retry (in seconds) shall
             *        be configured via this parameter
             * \param shall_stop if this flag is set, all pending mails are suppressed and the SMTP session
             *        is closed. This is the proper way to stop the sending process and to exit the application.
             */
            virtual void OnMessageStatus(const wxEmailMessage::MessageId&           WXUNUSED(message_id),
                                         SendingStatus_t                            WXUNUSED(status),
                                         unsigned long                              WXUNUSED(nb_pending_messages),
                                         unsigned long                              WXUNUSED(nb_retry_messages),
                                         const std::list<wxEmailMessage::Address>&  WXUNUSED(rejected_addresses),
                                         const std::list<wxEmailMessage::Address>&  WXUNUSED(accepted_addresses),
                                         bool                                       WXUNUSED(can_retry),
                                         bool&                                      shall_retry,
                                         unsigned long&                             WXUNUSED(retry_delay_s),
                                         bool&                                      shall_stop)
            {
               shall_retry = false;
               shall_stop = false;
            }

            /*!
             * This defines all possible status of server communication process.
             */
            typedef enum
            {
               StatusOK,                     /*!< The communication with server ended successfully */
               StatusTimeout,                /*!< A timeout occurred while communicating with the server */
               StatusDisconnect,             /*!< The connection was lost during communication with server */
               StatusRetry,                  /*!< The server requests a retry of sending process */
               StatusError,                  /*!< The server rejected a command, which ended the sending process */
               StatusUserAbort,              /*!< The user requested an abort */
               StatusInvalidUserNamePassword,/*!< The provided user name or password is invalid */
               StatusSslError                /*!< The Ssl session could not be initiated */
            } DisconnectionStatus_t;

            /*!
             * This function is invoked each time the connection to the server is closed.
             * Note that multiple messages can be sent without any call to this function as,
             * during a session with the SMTP server, more than one message can be sent.
             *
             * \param status the status of the disconnection
             * \param nb_pending_messages the number of messages that are still pending
             * \param nb_retry_messages the number of messages that are marked as retry
             * \param shall_retry indicates if the connection shall be retried.
             * \param retry_delay_s the retry delay, in seconds
             */
            virtual void OnDisconnect(DisconnectionStatus_t WXUNUSED(status),
                                      unsigned long         WXUNUSED(nb_pending_messages),
                                      unsigned long         WXUNUSED(nb_retry_messages),
                                      bool&                 shall_retry,
                                      unsigned long&        WXUNUSED(retry_delay_s))
            {
               shall_retry = false;
            }

            virtual ~Listener() {}
      };

      /*!
       * This is the default constructor of the SMTP client.
       *
       * \param host The host name of the SMTP server
       * \param port the port associated to the SMTP server
       * \param listener the ::wxSMTP::Listener that will be used to monitor the SMTP session. If NULL, no listener is used.
       */
      wxSMTP(const wxString& host,
             unsigned short port = 25,
             bool ssl_enabled = false,
             Listener* listener = NULL);

      /*!
       * This enumerates all implemented authentication schemes available
       */
      typedef enum
      {
         NoAuthentication, /*!< No authentication required */
         AutodetectAuthenticationMethod,
         CramMd5Authentication, /*!< SASL CRAM-MD5 authentication scheme */
         PlainAuthentication,
         LoginAuthentication
      } AuthenticationScheme_t;

      /*!
       * This function allows configuring the authentication scheme used for connection
       * to the SMTP server
       *
       * \param authentication_scheme The ::AuthenticationScheme_t used for the connection
       * \param user_name The user name to be used, if requested
       * \param password The password to be used, if requested
       * \param ssl_enabled enables SSL connection
       */
      void ConfigureAuthenticationScheme(AuthenticationScheme_t authentication_scheme,
                                         const wxString& user_name = wxT(""),
                                         const wxString& password = wxT(""),
                                         bool ssl_enabled = false);

      /*!
       * This function posts the message in the list of messages to be sent.
       *
       * Note that when the function returns, the message is not sent. In order to monitor
       * the status of the message, a ::wxSMTP::Listener shall be registered.
       *
       * \param message The ::wxEmailMessage that shall be sent.
       */
      void SendMessage(const wxEmailMessage& message, bool shall_start_sending_process = true);

      /*!
       * This function restarts the sending process. This can be useful if, for example,
       * sending process was aborted due to a communication error and that there are pending
       * messages
       */
      void RestartSendingProcess();

      /*!
       * This function performs a dummy connection to the server.
       *
       * If the client was already connected, this function has no effect.
       *
       * After the call to this function, the ::wxSMTP::Listener::OnDisconnect callback
       * will be invoked, which will allow checking if connection parameters are
       * correct.
       */
      void TestConnection();

      /*!
       * This method allows determining if the SMTP client is running.
       *
       * \return true if the client is currently connected to the server, false otherwise.
       */
      bool IsRunning() const;

      /*!
       * This method allows aborting all sending process in progress.
       *
       * Once this method has been called, the client is not immediately disconnected of the
       * server/ The user shall wait the reception of the callback ::wxSMTP::Listener::OnDisconnect to
       * be sure the disconnection is effective.
       *
       * Note that this function has no effect if the client was not connected. In this case,
       * the :Listener::OnDisconnect will never be invoked.
       *
       * The proper code for an immediate disconnection of the server is as follows :
       *
       * smtp.FlushAllMessages();
       * if (smtp.IsRunning())
       * {
       *    // wait the ::wxSMTP::Listener::OnDisconnect callback to leave application
       * }
       * else
       * {
       *    // you can safely exit application now
       * }
       */
      void FlushAllMessages();

      /*!
       * This is the destructor of the class.
       *
       * Note that, in order to perform a proper disconnection of the client,
       * the destructor cannot be invoked while a communication is in progress.
       *
       * See ::FlushAllMessages for instructions on how terminating a running session
       */
      ~wxSMTP();

   private:

      class wxSmtpEmailMessage : public wxEmailMessage
      {
         public:

            /*!
             * This is the default constructor of the SMTP mail.
             *
             * \param from the sender wxEmailMessage::Address
             * \param subject The subject of the mail
             * \param text the content of the mail, in raw text from. To add HTML part, see method wxEmailMessage::AddAlternativeHtmlBody
             * \param a unique id that will identify the message
             */
            wxSmtpEmailMessage(const wxEmailMessage& email_msg);
            wxSmtpEmailMessage();

            /**
             * \internal
             *
             * Writes the message as one blob, MIME encoded, if necessary,
             * e.g. for sending it with SMTP.
             * \param out The output stream to which the email has to be written.
             */
            void Encode(wxOutputStream& out);

            /*!
             * \internal
             *
             * This function indicates if mail has still recipients
             */
            bool HasStillRecipients() const;

            /*!
             * \internal
             *
             * This function is used to determine if the user has already extracted
             * somme recipients.
             *
             * \return false until GetNextRecipient is called, true after
             */
            bool RecipientsAlreadySent() const;

            /*!
             * \internal
             *
             * This function enumerates all recipients of the message.
             * It is used internally by the wxSMTP function to get all recipients
             * of the message.
             *
             * Once this method is invoked, the recipient is removed from the list
             * of recipients returned by GetNextRecipient.
             *
             * Note that the user can re-insert previously removed recipients by invoking
             * the method ::ReinsertRecipient (which is useful if we shall retry sending
             * the mail)
             *
             * Note that the recipient is not removed from TO, CC or BCC list.
             *
             * \param rcpt The string that will contain next recipient address, if any
             * \return true if there is a new recipient, false otherwise
             */
            bool GetNextRecipient(Address& rcpt);

            /*!
             * \internal
             *
             * This function reinserts a recipient in the list of recipients tha will
             * be returned by GetNextRecipient.
             */
            void ReinsertRecipient(const Address& rcpt);

            /*!
             * \internal
             * This list contains all recipients that the ::GetNextRecipient function
             * shall still return
             */
            std::list<Address> m_rcptAttempt;

            /*!
             * \internal
             * This is used by ::RecipientsAlreadySent function
             */
            bool has_already_sent_recipients;
      };

      /*!
       * \internal
       *
       * This is the default listener used when the user does not register its own
       * one.
       */
      static Listener g_nullListener;

      /*!
       * \internal
       * This class is the base class of all SMTP states
       */
      class SMTPState : public wxCmdlineProtocol::State
      {
         public:
            /*!
             * This function is used to restart sending mails when FSM is in stop mode.
             * The major part of states will do nothing as mail sending process is already
             * running, but some will handle proper transitions.
             */
            virtual void onStartSendingMails(wxCmdlineProtocol& WXUNUSED(context)) const {}

            /*!
             * This function flushes all currently sent messages to abort sending process.
             */
            virtual void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const = 0;

            /*!
             * Just to be sure we have a virtual destructor
             */
            virtual ~SMTPState() {}
      };

      /*!
       * \internal
       *
       * When entering this state, the connection to the server is initiated and
       * a timeout is triggered. The flag wxSMTP#nb_recipients_successful is reset. The lists
       * wxSMTP#total_accepted_recipients and wxSMTP#total_rejected_recipients are cleared
       *
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li ::wxSMTP::HeloState If the server ACKS the connection
       *    \li ::wxSMTP::QuitState In case of timeout or NACK sent by server, the wxSMTP#disconnection_status flag is updated accordingly
       *    \li ::wxSMTP::ClosedState In case of disconnection by the server, the wxSMTP#disconnection_status flag is updated accordingly
       */
      class ConnectState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, the HELLO command is sent to server and
       * a timeout is triggered.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li ::wxSMTP::StartTLSState If the server ACKS the connection and ssl connection is requested
       *    \li ::wxSMTP::AuthenticateState If the server ACKS the connection and authentication is requested
       *    \li ::wxSMTP::SendMailFromState If the server ACKS the connection and no authentication is requested
       *    \li ::wxSMTP::QuitState In case of timeout or NACK sent by server, the wxSMTP#disconnection_status flag is updated accordingly
       *    \li ::wxSMTP::ClosedState In case of disconnection by the server, the wxSMTP#disconnection_status flag is updated accordingly
       */
      class HeloState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, the StartTLSState command is sent to server and
       * a timeout is triggered.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li ::wxSMTP::SSLNegociationState If the server ACKS the connection
       *    \li ::wxSMTP::QuitState In case of timeout or NACK sent by server, the wxSMTP#disconnection_status flag is updated accordingly
       *    \li ::wxSMTP::ClosedState In case of disconnection by the server, the wxSMTP#disconnection_status flag is updated accordingly
       */
      class StartTLSState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, the StartTLSState command is sent to server and
       * a timeout is triggered.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li ::wxSMTP::HeloState If the server ACKS the connection
       *    \li ::wxSMTP::QuitState In case of timeout or NACK sent by server, the wxSMTP#disconnection_status flag is updated accordingly
       *    \li ::wxSMTP::ClosedState In case of disconnection by the server, the wxSMTP#disconnection_status flag is updated accordingly
       */
      class SSLNegociationState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, the AUTHENTICATE  command is sent to server and
       * a timeout is triggered. The authentication_digest_sent is set to false
       *
       * When a response is received from server, if authentication_digest_sent is false, the digest is sent
       * accordingly to received response and flag authentication_digest_sent is set.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li ::wxSMTP::SendMailFromState If the server ACKS authentication
       *    \li ::wxSMTP::QuitState In case of timeout or NACK sent by server, the wxSMTP#disconnection_status flag is updated accordingly
       *    \li ::wxSMTP::ClosedState In case of disconnection by the server, the wxSMTP#disconnection_status flag is updated accordingly
       */
      class AuthenticateState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, We check content of wxSMTP#retry_messages_list. If some timers expired,
       * corresponding messages are removed from wxSMTP#retry_messages_list and inserted in wxSMTP#messages_to_send.
       *
       * We thus check if wxSMTP#messages_to_send is empty. If so, a direct
       * transition to ::wxSMTP::QuitState is performed and disconnection_status is set to ::wxSMTP::Listener::StatusOK.
       * Else, the FROM command is sent with data from the first element of wxSMTP#list messages_to_send and
       * the timer is triggered. If message has no recipients already sent (::wxSMTPEMail::RecipientsAlreadySent), lists
       * wxSMTP#total_accepted_recipients and wxSMTP#total_rejected_recipients are cleared.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li ::wxSMTP::RcptListState If the server ACKS the connection
       *    \li ::wxSMTP::QuitState In case of timeout. the disconnection_status flag is updated accordingly.
       *    \li In case of NACK different from retry sent by server, the message is poped from the list. The callback ::wxSMTP::Listener::OnMessageStatus is
       *        invoked. If user requests a retry, message is appended to retry list.
       *    \li ::wxSMTP::QuitState In case of retry NACK received from the server. disconnection_status is set to
       *        ::wxSMTP::Listener::StatusRetry.
       *    \li ::wxSMTP::ClosedState In case of disconnection by the server, the disconnection_status flag is updated accordingly
       */
      class SendMailFromState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, we reset flag wxSMTP#nb_pending_recipients and we send the RCPT command
       * on first recipient of first e-mail of the wxSMTP#messages_to_send list. The timeout is started.
       * wxSMTP#current_recipient is updated with recipient sent. wxSMTP#pending_accepted_recipients is
       * cleared.
       *
       * If there are no recipients to the mail, we generate a ::wxSMTP::Listener::OnMessageStatus callback with
       * ::wxSMTP::Listener::SendingFailed status and we remove the message from the list. ::wxSMTP::SendMailFromState is
       * then entered to process next messages.
       *
       * When leaving this state, the timer is stopped.
       *
       * When receiving a response from the server, the following process is performed :
       *    \li if server ACKS the connection, the timer is reset, wxSMTP#pending_accepted_recipients is incremented, and wxSMTP#current_recipient
       *    is appended to wxSMTP#pending_accepted_recipients. We thus check if there are still recipients;
       *       <ul>
       *             <li> If there are still recipients, a new RCPT command is emitted to this client,
       *                  and the variable wxSMTP#current_recipient is updated.</li>
       *             <li> If there are no more recipients, we switch to state ::wxSMTP::BeginDataState.</li>
       *       </ul>
       *    \li if server refuses the previous RCPT with invalid address, wxSMTP#current_recipient is appended
       *    to wxSMTP#total_rejected_recipients list. We then perform following action :
       *       <ul>
       *          <li> If there are still recipients, RCPT command is sent to next one and
       *               current_recipient is updated. </li>
       *          <li> If there are no more recipients :
       *                <ul>
       *                   <li> If wxSMTP#nb_pending_recipients > 0, we switch to ::wxSMTP::BeginDataState.</li>
       *                   <li> If wxSMTP#nb_pending_recipients == 0, we abort current sending process
       *                        (::wxSMTP::CancelMailSendingProcess).
       *                   </li>
       *                </ul></li>
       *       </ul>
       *    \li if server refuses the previous RCPT with too much addresses, wxSMTP#current_recipient is appended
       *        to the list of recipients we shall send message (::wxSmtpEMail::ReinsertRecipient) and we switch
       *        to state ::wxSMTP::BeginDataState
       *    \li if server refuses the previous RCPT with any other error, wxSMTP#current_recipient is appended
       *        to the list of recipients we shall send message (::wxSmtpEMail::ReinsertRecipient) and
       *        message sending process is cancelled (::wxSMTP::CancelMailSendingProcess).
       *
       * The following transitions are implemented:
       *    \li ::wxSMTP::QuitState In case of timeout or NACK sent by server, the sending process is aborted (::wxSMTP::CancelMailSendingProcess).
       *    \li ::wxSMTP::ClosedState In case of disconnection by the server, the wxSMTP#disconnection_status flag is updated accordingly
       */
      class RcptListState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, The DATA command is sent to server. The timer is started.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li ::wxSMTP::DataState If the server ACKS the connection
       *    \li In all other cases, all recipients in wxSMTP#pending_accepted_recipients
       *        are reinserted in remaining recipients (::wxSmtpEMail::ReinsertRecipient). The callback
       *        ::wxSMTP::Listener::OnMessageStatus is generated. If flag retry is set, the message is appended
       *        to the list wxSMTP#retry_messages_list. The message is popped from wxSMTP#messages_to_send.
       *        <ul>
       *          <li> if timeout or NACK different from retry sent by server, we switch to ::wxSMTP::QuitState. The wxSMTP#disconnection_status flag is updated accordingly </li>
       *          <li> if retry NACK and termination flag is not set, we switch to ::wxSMTP::SendMailFromState </li>
       *          <li> if retry NACK and termination flag is not set, we switch to ::wxSMTP::QuitState The wxSMTP#disconnection_status flag is updated accordingly </li>
       *          <li> if disconnection, we switch to ::wxSMTP::ClosedState. The wxSMTP#disconnection_status flag is updated accordingly </li>
       *        </ul>
       */
      class BeginDataState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, The complete message data is sent to server. The timer is started.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li If the server sends an ACK, we check if the message has still recipients :
       *       <ul>
       *          <li> If the message has still recipients, all elements of wxSMTP#pending_accepted_recipients
       *               are inserted in wxSMTP#total_accepted_recipients. The state ::wxSMTP::SendMailFromState
       *               is entered </li>
       *          <li> If the message has no more recipient, the callback ::wxSMTP::Listener::OnMessageStatus
       *               is generated. The message is removed from list wxSMTP#messages_to_send. </li>
       *       </ul>
       *    \li In all other cases, we cancel mail sending process (wxSMTP::CancelMailSendingProcess) with
       *        appropriate status code.
       */
      class DataState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, the QUIT command is sent to server and
       * a timeout is triggered.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li timeout, NACK or disconnect : the state ::wxSMTP::ClosedState is entered and wxSMTP#disconnection_status is update
       *    \li ACK : the state ::wxSMTP::ClosedState is entered
       */
      class QuitState : public SMTPState
      {
         public:
            void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const;
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onStartSendingMails(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * \internal
       *
       * When entering this state, the connection is closed and, if wxSMTP#shall_trigger_disconnection_callback
       * is set, the callback ::wxSMTP::Listener::OnDisconnect is call with status wxSMTP#disconnection_status.
       *
       * wxSMTP#shall_trigger_disconnection_callback is then updated to true.
       *
       * The timer is updated with the first retry delay.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       *    \li timeout The corresponding message is removed from wxSMTP#retry_messages_list and inserted
       *        in wxSMTP#messages_to_send. The state ::wxSMTP::ConnectState is entered.
       *    \li UserRequest : The state ::wxSMTP::ConnectState is entered.
       */
      class ClosedState : public SMTPState
      {
         public:
            void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onStartSendingMails(wxCmdlineProtocol& WXUNUSED(context)) const;
            void onFlushMessages(wxCmdlineProtocol& WXUNUSED(context)) const;
      };

      /*!
       * This function cancels the current mail sending process.
       *
       * It invokes the Listener::OnMessageStatus. If accept_retry is set and users request
       * retry, mail is reinserted in retry list, with all pending recipients reinserted.
       *
       * If the user selects continue, the state ::wxSMTP::SendMailFromState is entered. Else
       * ::wxSMTP::g_quitState is entered.
       */
      void CancelMailSendingProcess(Listener::SendingStatus_t status, bool accept_retry, bool shall_quit, bool shall_send_quit, Listener::DisconnectionStatus_t disconnection_status);

      /*!
       * \internal
       *
       * Counts the number of messages that are in retry list.
       * Note that, as this list contains also reconnection info, we have dedicated function to count messages
       *
       * \return Number of messages ready for retry
       */
      unsigned long GetNbRetryMessages() const;

      static const ConnectState        g_connectState;
      static const HeloState           g_heloState;
      static const StartTLSState       g_startTlsState;
      static const SSLNegociationState g_sslNegociationState;
      static const AuthenticateState   g_authenticateState;
      static const SendMailFromState   g_sendMailFromState;
      static const RcptListState       g_rcptListState;
      static const BeginDataState      g_beginDataState;
      static const DataState           g_dataState;
      static const QuitState           g_quitState;
      static const ClosedState         g_closedState;

      /*!
       * \internal
       * Stores the disconnection status. Used to record the reason of disconnection
       * before reaching the ::ClosedState
       */
      Listener::DisconnectionStatus_t disconnection_status;

      /*!
       * \internal
       * This flag is used to don't trigger the callback at initialisation, when
       * the ::wxSMTP::ClosedState is first entered.
       */
      bool shall_trigger_disconnection_callback;

      /*!
       * \internal
       * This list contains all messages to be sent. The first one is the currently
       * sent, if we are in sending process. Messages are inserted at the end.
       * Note that pending retry messages are not stored in this list, but in
       * #retry_messages_list.
       */
      std::list<wxSmtpEmailMessage> messages_to_send;

      /*!
       * \internal
       * This variable is used to store the number of recipients to which the mail
       * has been successfuly sent. This is useful in multiple sending process (i.e.
       * when there are too much recipients for a single sending process) in order
       * to determine if the message has been sent at at least one person.
       */
      unsigned long nb_recipients_successful;

      /*!
       * \internal
       * This variable counts the number of recipients for which RCPT command has been
       * accepted, but for which mail was not effectively sent.
       */
      unsigned long nb_pending_recipients;

      /*!
       * \internal
       * This list contains address of all recipients for which RCPT command has been
       * accepted, but for which mail was not effectively sent.
       */
      std::list<wxEmailMessage::Address> pending_accepted_recipients;

      /*!
       * \internal
       * This list contains address of all recipients for which mail has been
       * effectively sent.
       */
      std::list<wxEmailMessage::Address> total_accepted_recipients;

      /*!
       * \internal
       * This list contains address of all recipients for which mail has been
       * definitively rejected.
       */
      std::list<wxEmailMessage::Address> total_rejected_recipients;

      /*!
       * \internal
       * This variable contains the address of the recipient that is currenly handled
       * by the RCPT state.
       */
      wxEmailMessage::Address current_recipient;

      /*!
       * \internal
       * This structure represents a message that is marked for retry
       */
      typedef struct
      {
         wxDateTime           retry_time;       /*! This is the time where the retry shall occur */
         bool                 is_reconnection;  /*! If this flag is set, it means that we only need to reconnect, but no message shall be reinserted in the list */
         wxSmtpEmailMessage   message;          /*! This is the message that shall be retried */
      } RetryInfo_t;

      /*!
       * \internal
       * This list contains all messages that shall be retried. This list is order by
       * ::wxSMTP::RetryInfo_t::retry_time.
       */
      std::list<RetryInfo_t> retry_messages_list;

      /*!
       * \internal
       * This is the listener that shall be used to notify events to user
       */
      Listener* m_pListener;

      /*!
       * \internal
       * timeout used for all states, expressed in seconds
       */
      static const unsigned long timeout;

      AuthenticationScheme_t authentication_scheme;
      AuthenticationScheme_t current_authentication_scheme;
      wxString user_name;
      wxString password;
      bool ssl_enabled;
      bool shall_enter_ssl;
      bool authentication_digest_sent;
      wxString authentication_line;

      wxString ComputeAuthenticationDigest(const wxString& digest);

	  wxString DecodeBase64(const wxString& data);
	  wxString EncodeBase64(const wxString& data);
};

#endif
