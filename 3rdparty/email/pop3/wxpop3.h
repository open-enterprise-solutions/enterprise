/////////////////////////////////////////////////////////////////////////////
// Name:        wxpop3.h
// Purpose:     This implements a pop3 client
// Author:      Brice André
// Created:     2010/12/03
// RCS-ID:      $Id: mycomp.h 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2010 Brice André
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////


#ifndef _WX_POP3_H_
#define _WX_POP3_H_

// wxWidgets headers
#include <wx/protocol/protocol.h>

#include "../wxsmtpdef.h"

#include "../utils/wxcmdprot.h"

/*!
 *
 * This class implements a POP3 client.
 *
 * \section pop3_usage Usage
 *
 * The class can be created once for all accesses performed to a pop3 server
 * with the same user. A new connection will be established with the server each
 * time a new operation will be triggered on this server.
 *
 * Note that it is not recommended to instantiate several wxPOP3 objects on the same
 * POP3 server with the same user as parallel access to same mailbox is not supported
 * by servers.
 *
 * In order to avoid blocking the gui during communication with the server, all operations
 * are performed asynchronously. In order to be notified of an operation status (or in order
 * to interact with it), an overloaded wxPOP3Listener object shall be provided.
 *
 * All functions of the provided wxPOP3Listener are invoked in the context of the main thread
 * and thus, it is safe to perform gui operations from it.
 *
 * All operations end with a call to the wxPOP3Listener::OnOperationFinished indicating the
 * status of the operation. When an operation is in progress, another operation cannot be
 * performed until the first one invokes this callback.
 *
 * The user can check if an operation is in progress by using the function wxPOP3::OperationInProgress
 *
 * \if coders
 *
 * \section pop3_internal Internal
 *
 * This class uses the ::wxCmdlineProtocol class for implementation. The following graph represents
 * the implemented state machine :
 *
 * \dot
 * digraph LauncherFSM {
 *
 *    node [shape=record, fontname=Helvetica, fontsize=10];
 *
 *    InitialState [ label="InitialState" URL="\ref wxPOP3::InitialState"];
 *    SslNegociationState [ label="SslNegociationState" URL="\ref wxPOP3::SslNegociationState"];
 *    HelloState [ label="HelloState" URL="\ref wxPOP3::HelloState"];
 *    AuthorisationState [ label="AuthorisationState" URL="\ref wxPOP3::AuthorisationState"];
 *    GetMessagesListState [ label="GetMessagesListState" URL="\ref wxPOP3::GetMessagesListState"];
 *    HandlingMessage [ label="HandlingMessage" URL="\ref wxPOP3::HandlingMessage"];
 *    DownloadingMessage [ label="DownloadingMessage" URL="\ref wxPOP3::DownloadingMessage"];
 *    SuppressingMessage  [ label="SuppressingMessage" URL="\ref wxPOP3::SuppressingMessage"];
 *    ExitState [ label="ExitState" URL="\ref wxPOP3::InitialState"];
 *
 *    InitialState -> ExitState [ label="KO|timeout|\ndisconnect", arrowhead="open", style="dashed" ];
 *    InitialState -> HelloState [ label="OK-NoSSl", arrowhead="open", style="dashed" ];
 *    InitialState -> StartSslState [ label="OK-SSl", arrowhead="open", style="dashed" ];
 *
 *    SslNegociationState -> ExitState [ label="KO|timeout|\ndisconnect", arrowhead="open", style="dashed" ];
 *    SslNegociationState -> HelloState [ label="OK", arrowhead="open", style="dashed" ];
 *
 *    HelloState -> ExitState [ label="KO|timeout|\ndisconnect", arrowhead="open", style="dashed" ];
 *    HelloState -> AuthorisationState [ label="OK", arrowhead="open", style="dashed" ];
 *
 *    AuthorisationState -> ExitState [ label="OK_test|KO|\ntimeout|disconnect", arrowhead="open", style="dashed" ];
 *    AuthorisationState -> GetMessagesListState [ label="OK_download", arrowhead="open", style="dashed" ];
 *
 *    GetMessagesListState -> ExitState [ label="KO|timeout|\ndisconnect", arrowhead="open", style="dashed" ];
 *    GetMessagesListState -> HandlingMessage [ label="OK", arrowhead="open", style="dashed" ];
 *
 *    HandlingMessage -> ExitState [ label="KO|timeout|\ndisconnect|user_exit", arrowhead="open", style="dashed" ];
 *    HandlingMessage -> SuppressingMessage [ label="OK_suppress", arrowhead="open", style="dashed" ];
 *    HandlingMessage -> DownloadingMessage [ label="OK_download_\noptional_suppress", arrowhead="open", style="dashed" ];
 *
 *    DownloadingMessage -> ExitState [ label="KO|timeout|\ndisconnect|OK_no_more_msg|\nOK_user_abort", arrowhead="open", style="dashed" ];
 *    DownloadingMessage -> SuppressingMessage [ label="OK_suppress", arrowhead="open", style="dashed" ];
 *    DownloadingMessage -> HandlingMessage [ label="OK_not_suppress", arrowhead="open", style="dashed" ];
 *
 *    SuppressingMessage -> ExitState [ label="KO|timeout|\ndisconnect|OK_no_more_msg", arrowhead="open", style="dashed" ];
 *    SuppressingMessage -> HandlingMessage [ OK_still_msg", arrowhead="open", style="dashed" ];
 * \enddot
 *
 * \endif
 */
class WXDLLIMPEXP_SMTP wxPOP3 : public wxCmdlineProtocol
{
   public:

      /*!
       * This class allows an application to be notified of the current progress as well as
       * interacting with server during a POP3 session.
       *
       * It shall be extended by the application using the ::wxPOP3 class in order to perform
       * requested operations.
       */
      class WXDLLIMPEXP_SMTP Listener
      {
         public:

            /*!
             * \internal
             * Just to avoid problems for inherited classes...
             */
            virtual ~Listener() {}

            /*!
             * Enumerates all possible command statuses
             */
            typedef enum
            {
               Succeed = 0,      /*!< Command successfully terminated */
               Abort,            /*!< Command aborted by user */
               Timeout,          /*!< Command aborted : timeout during server communication */
               Error,            /*!< Command aborted : server generated error */
               InvalidUserPass   /*!< Command aborted : invalid user name or password */
            } Status_t;

            typedef enum
            {
               NoExtraction,
               ExtractHeader,
               ExtractFullMessage
            } ExtractionMode_t;

            /*!
             * This function is invoked each time a triggered ::wxPOP3 operation finished.
             *
             * @param status the Listener::Status_t exit condition of the operation
             */
            virtual void OnOperationFinished(Status_t  WXUNUSED(status)) {}

            /*!
             * This function is invoked during a ::wxPOP3::DownloadMessages operation each time a message present on the
             * POP3 server is identified.
             *
             * Progress information is provided so that the user can display a progress indicator.
             *
             * The user can decide to download or suppress the message by setting the provided flags. It can also
             * abort the download progress and terminate the current POP3 session.
             *
             * \param message_id A ::wxPOP3EmailMessage::MessageId that allows identifying the message currently received
             * \param current The currently extracted message id. This number is comprised between [1,total].
             * \param total The total number of messages available on the server
             * \param extraction_mode if the user sets this flag to Listener::ExtractHeader or Listener::ExtractFullMessage,
             *                        the (complete or just header) message will be extracted. if the user sets this
             *                        flag to Listener::NoExtraction, the session will directly jump to next message.
             *                        Default behaviour of the base class sets this flag to true
             * \param shall delete if the user sets this flag to true, the message will be marked for deletion. Note that
             *                     this flag can safely be set with 'shall_extract' flag. Default behaviour of the base class
             *                     sets this flag to Listener::ExtractFullMessage
             * \param shall_stop if the user sets this flag to true, the operation is immediately aborted. In this case, the
             *                   previous flags are not taken into account. Default behaviour of the base class sets this flag
             *                   to false.
             */
            virtual void OnFoundMessage(const wxEmailMessage::MessageId& WXUNUSED(message_id),
                                        unsigned long WXUNUSED(current),
                                        unsigned long WXUNUSED(total),
                                        ExtractionMode_t& extraction_mode,
                                        bool& shall_delete,
                                        bool& shall_stop)
            {
               extraction_mode = ExtractFullMessage;
               shall_delete = false;
               shall_stop = false;
            }

            /*!
             * This function is invoked during a ::wxPOP3::DownloadMessages operation each time a message is downloaded from
             * the server.
             *
             * @param message the content of the downloaded message. The function takes ownership of this message and shall delete it.
             * @param shall_stop If the user sets this flag to true, the download operation is immediately aborted. The default
             *                   behaviour of the base class sets this flag to false.
             */
            virtual void OnMessageContent(wxEmailMessage* message,
                                          bool& shall_stop)
            {
               delete message;
               shall_stop = false;
            }

            /*!
             * This function is invoked before messages are handled. The provided list contains all ids of messages
             * present on the server.
             * The user can freely remove all entries from the list he does not want to be handled by the pop3 session.
             */
            virtual void OnFilterHandledMessages(std::list<wxEmailMessage::MessageId>& WXUNUSED(handled_messages))
            {}
      };

      /*!
       * This enumerates all implemented authentication schemes available
       */
      typedef enum
      {
         UserPassAuthenticationMethod,
         APopAuthenticationMethod
      } AuthenticationScheme_t;

      /*!
       * This is the default constructor of the POP3 client.
       *
       * @param client_name the name of the pop3 client account
       * @param client_password the password of the pop3 client account
       * @param server_name the pop3 server name
       * @param port the pop3 server port (default 110)
       * @param default_listener the default wxPOP3Listener to use with the library.
       */
      wxPOP3(wxString client_name,
             wxString client_password,
             wxString server_name,
             unsigned long port = 110,
             AuthenticationScheme_t authentication_scheme = UserPassAuthenticationMethod,
             bool ssl_enabled = false,
             Listener* default_listener = NULL);

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
       * Indicates if an operation is currently in progress
       *
       * @return true if an operation is in progress, false otherwise
       */
      bool OperationInProgress();

      /*!
       * This operation performs a dummy connection to the server.
       *
       * It can be used to check connection parameters, or to test
       * if server is alive.
       *
       * This operation will trigger the following callbacks :
       *    - wxPOP3Listener::OnOperationFinished : indicates the status
       *      of the connection test
       *
       * @param operation_listener if not NULL, will override the default listener to use.
       * @return true if the operation was successfully triggered, false if another
       *         operation was in progress.
       */
      bool CheckConnection(Listener* operation_listener = NULL);

      /*!
       * This operation performs a complete download of the messages stored on the server
       *
       * For each message present, it invokes the following callbacks :
       *    - wxPOP3Listener::FoundMessage : If the user sets the parameter shall_stop to true, the
       *      complete processing will stop and next callback invoked will be wxPOP3Listener::OnOperationFinished.
       *      Note that, in this case, all messages marked for deletion will be deleted.
       *      If the user sets the parameter shall_extract to false, the next callbacks will not be invoked for this
       *      message and processing will directly switch to next message of the server.
       *    - wxPOP3Listener::MessageContent : if the user sets the parameter shall_stop to true, the
       *      complete processing will stop and next callback invoked will be wxPOP3Listener::OnOperationFinished.
       *
       * When all messages have been received, or when the user requests a stop of the operation, the following
       * callback is invoked :
       *    - wxPOP3Listener::OnOperationFinished : indicates the status of the messages download.
       *
       */
      bool DownloadMessages(Listener* operation_listener = NULL);

   private:

      /*!
       * This state is entered when the client initiates a connection to the server.
       * We immediately leave this state to SslNegociationState if an ssl connection
       * is requested of for Hello state if not.
       */
      class InitialState : public wxCmdlineProtocol::State
      {
         public:
            void onDisconnect(wxCmdlineProtocol& context) const;
            void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
            void onTimeout(wxCmdlineProtocol& context) const;
            void onEnterState(wxCmdlineProtocol& context) const;
            void onLeaveState(wxCmdlineProtocol& context) const;
      };

      /*!
       * \internal
       *
       * When entering this state, the SSL session is negociated. A timer is started.
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       * \li \c timeout/disconnect If the server does not respond before timeout expires or if
       *                           the connection is lost, the
       *                           ::wxPOP3::ExitState is entered. The function
       *                           Listener::OnOperationFinished is
       *                           invoked with ::wxPOP3::Listener::Timeout status
       * \li \c KO If the ssl session cannot be established.
       *           The function Listener::OnOperationFinished is
       *           invoked with ::wxPOP3::Listener::Error status
       * \li \c OK If the ssl session is properly established, the ::wxPOP3::HelloState is entered.
       */
      class SslNegociationState : public wxCmdlineProtocol::State
      {
         void onDisconnect(wxCmdlineProtocol& context) const;
         void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
         void onTimeout(wxCmdlineProtocol& context) const;
         void onEnterState(wxCmdlineProtocol& context) const;
         void onLeaveState(wxCmdlineProtocol& context) const;
      };

      /*!
       * \internal
       *
       * When entering this state, the we wait for the ACK message received from the server.
       * A timer is started
       *
       * When leaving this state, the timer is stopped.
       *
       * The following transitions are implemented:
       * \li \c timeout/disconnect If the server does not respond before timeout expires or if
       *                           the connection is lost, the
       *                           ::wxPOP3::ExitState is entered. The function
       *                           Listener::OnOperationFinished is
       *                           invoked with ::wxPOP3::Listener::Timeout status
       * \li \c KO If the server send a NACK, the ::wxPOP3::ExitState is entered.
       *           The function Listener::OnOperationFinished is
       *           invoked with ::wxPOP3::Listener::Error status
       * \li \c OK If the server send an ACK, the ::wxPOP3::AuthorisationState is entered.
       */
      class HelloState : public wxCmdlineProtocol::State
      {
         void onDisconnect(wxCmdlineProtocol& context) const;
         void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
         void onTimeout(wxCmdlineProtocol& context) const;
         void onEnterState(wxCmdlineProtocol& context) const;
         void onLeaveState(wxCmdlineProtocol& context) const;
      };

      /*!
       * When entering this state, an APOP command is initiated. If this APOP command fails, a
       * USER/PASS command is initiated.
       *
       * The following transitions are implemented:
       * \li \c timeout/disconnect If the server does not respond before timeout expires or if
       *                           the connection is lost, the
       *                           ::wxPOP3::ExitState is entered. The function
       *                           Listener::OnOperationFinished is
       *                           invoked with ::wxPOP3::Listener::Timeout status
       * \li \c OK_test If the server send an ACK and we are in a CheckConnection operation,
       *                    the ::wxPOP3::ExitState is entered. The function
       *                           Listener::OnOperationFinished is
       *                           invoked with ::wxPOP3::Listener::Succeed status
       * \li \c KO If the server rejected both APOP and USER/PASS connection methods,
       *           the ::wxPOP3::ExitState is entered.
       *           The function Listener::OnOperationFinished is
       *           invoked with ::wxPOP3::Listener::InvalidUserPass status
       * \li \c OK_download If the server send an ACK and we are in a DownloadMessages operation,
       *                    the ::wxPOP3::GetMessagesListState is entered;
       */
      class AuthorisationState : public wxCmdlineProtocol::State
      {
         public:
            void onConnect(wxCmdlineProtocol& context) const;
            void onDisconnect(wxCmdlineProtocol& context) const;
            void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
            void onTimeout(wxCmdlineProtocol& context) const;
            void onEnterState(wxCmdlineProtocol& context) const;
            void onLeaveState(wxCmdlineProtocol& context) const;
      };

      /*!
       * When this state is entered, it initiates a LIST command and clears the content of
       * messages_list list.
       *
       * This function will fullfill the messages_list with all messages contained on the server.
       *
       * The following transitions are implemented:
       * \li \c timeout If the server does not respond before timeout expires, the
       *                ::wxPOP3::ExitState is entered. The function
       *                Listener::OnOperationFinished is
       *                invoked with ::wxPOP3::Listener::Timeout status
       * \li \c KO/disconnect If the server rejected the LIST method, or if a disconnection occured
       *                      the ::wxPOP3::ExitState is entered.
       *                      The function Listener::OnOperationFinished is
       *                      invoked with ::wxPOP3::Listener::Error status
       * \li \c OK If the server send an ACK, the function then enters the ::wxPOP3::HandlingMessage state.
       *
       */
      class GetMessagesListState : public wxCmdlineProtocol::State
      {
         public:
            void onConnect(wxCmdlineProtocol& context) const;
            void onDisconnect(wxCmdlineProtocol& context) const;
            void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
            void onTimeout(wxCmdlineProtocol& context) const;
            void onEnterState(wxCmdlineProtocol& context) const;
            void onLeaveState(wxCmdlineProtocol& context) const;
      };

      /*!
       * When this function is entered, an UIDL command is initiated on the first element of the
       * messages_list list. If the list is empty, a direct transition to state ExitState is performed
       * with status ::wxPOP3::Listener::Succeeded.
       *
       * Note that this command is an optional command that is not
       * necessarily implemented on all POP3 servers. So, it's not an error if the server
       * sends a NACK.
       *
       * The following transitions are implemented:
       * \li \c timeout If the server does not respond before timeout expires, the
       *                ::wxPOP3::ExitState is entered. The function
       *                Listener::OnOperationFinished is
       *                invoked with ::wxPOP3::Listener::Timeout status
       * \li \c disconnect If a disconnection occured
       *                   the ::wxPOP3::ExitState is entered.
       *                   The function Listener::OnOperationFinished is
       *                   invoked with ::wxPOP3::Listener::Error status
       * \li \c OK/KO In both cases, the function wxPOP3::Listener::OnFoundMessage is invoked.
       *              <ul>
       *                   <li> If the user requests an abort, ::wxPOP3::ExitState is entered and the function
       *                   Listener::OnOperationFinished is invoked with ::wxPOP3::Listener::Abort.</li>
       *                   <li> If the user selects extraction, the instance variable shall_suppress and extraction_mode are
       *                   updated and the state ::wxPOP3::DownloadingMessage is entered.</li>
       *                   <li> If only the flag shall_suppress is issued, the state ::wxPOP3::SuppressingMessage is
       *                   entered.</li>
       *                   <li> If not flag is set, the id is suppressed from list messages_list and this state is re-entered.</li>
       *             </ul>
       */
      class HandlingMessage : public wxCmdlineProtocol::State
      {
         public:
            void onConnect(wxCmdlineProtocol& context) const;
            void onDisconnect(wxCmdlineProtocol& context) const;
            void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
            void onTimeout(wxCmdlineProtocol& context) const;
            void onEnterState(wxCmdlineProtocol& context) const;
            void onLeaveState(wxCmdlineProtocol& context) const;
      };

      /*!
       * When this state is entered, a command RETR is initiated on the first element of
       * messages_list, and content of message_content is cleared.
       *
       * This command will store the complete message in the message_content variable.
       *
       * The following transitions are implemented:
       * \li \c timeout If the server does not respond before timeout expires, the
       *                ::wxPOP3::ExitState is entered. The function
       *                Listener::OnOperationFinished is
       *                invoked with ::wxPOP3::Listener::Timeout status
       * \li \c disconnect/KO If a disconnection occured or if the server returned a NACK,
       *                      the ::wxPOP3::ExitState is entered.
       *                      The function Listener::OnOperationFinished is
       *                      invoked with ::wxPOP3::Listener::Error status
       * \li \c OK If the message is correctly received, the function wxPOP3::Listener::OnMessageContent
       *           is invoked.
       *           <ul>
       *                <li> If the user requests an abort, the state ::wxPOP3::ExitState is entered and
       *                the function Listener::OnOperationFinished is invoked with ::wxPOP3::Listener::Abort status.</li>
       *                <li> If the user does not request abort and the flag shall_suppress is set, the state
       *                wxPOP3::SuppressingMessage is entered.</li>
       *                <li> If the user does not request abort and the flag shall_suppress is not set, the first
       *                element of messages_list is removed and the state wxPOP3::HandlingMessage is entered.</li>
       *            </ul>
       */
      class DownloadingMessage : public wxCmdlineProtocol::State
      {
         public:
            void onConnect(wxCmdlineProtocol& context) const;
            void onDisconnect(wxCmdlineProtocol& context) const;
            void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
            void onTimeout(wxCmdlineProtocol& context) const;
            void onEnterState(wxCmdlineProtocol& context) const;
            void onLeaveState(wxCmdlineProtocol& context) const;
      };
      friend class DownloadingMessage;

      /*!
       * When this state is entered, a DELE command is issued on the first elemenet of list messages_list.
       *
       * The following transitions are implemented:
       * \li \c timeout If the server does not respond before timeout expires, the
       *                ::wxPOP3::ExitState is entered. The function
       *                Listener::OnOperationFinished is
       *                invoked with ::wxPOP3::Listener::Timeout status
        * \li \c disconnect/KO If a disconnection occured or if the server returned a NACK,
       *                      the ::wxPOP3::ExitState is entered.
       *                      The function Listener::OnOperationFinished is
       *                      invoked with ::wxPOP3::Listener::Error status
       * \li \c OK the first element of the list messages_list is removed and the ::wxPOP3::HandlingMessage
       *           is entered.
       */
      class SuppressingMessage : public wxCmdlineProtocol::State
      {
         public:
            void onConnect(wxCmdlineProtocol& context) const;
            void onDisconnect(wxCmdlineProtocol& context) const;
            void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
            void onTimeout(wxCmdlineProtocol& context) const;
            void onEnterState(wxCmdlineProtocol& context) const;
            void onLeaveState(wxCmdlineProtocol& context) const;
      };

      /*!
       * This state is entered when the operation is finished. All connection stuff is cleaned
       * and we remain in this state.
       */
      class ExitState : public wxCmdlineProtocol::State
      {
         public:
            void onEnterState(wxCmdlineProtocol& context) const;
            void onDisconnect(wxCmdlineProtocol& context) const;
            void onResponse(wxCmdlineProtocol& context, const wxString& line) const ;
            void onTimeout(wxCmdlineProtocol& context) const;
      };

      static InitialState           initial_state;
      static HelloState             hello_state;
      static SslNegociationState    ssl_negociation_state;
      static AuthorisationState     authorisation_state;
      static GetMessagesListState   get_message_list_state;
      static HandlingMessage        handling_message_state;
      static DownloadingMessage     downloading_message_state;
      static SuppressingMessage     suppressing_message_state;
      static ExitState              exit_state;

      std::list<wxEmailMessage::MessageId> messages_list;
      bool in_uid_list;
      bool shall_suppress;
      wxString message_content;
      wxString session_digest;

      static Listener default_class_listener;
      Listener* default_instance_listener;
      Listener* current_operation_listener;

      wxString user_name;
      wxString user_password;

      bool ssl_enabled;

      enum
      {
         SentApopCmdStatus,
         SentUserCmdStatus,
         SentPassCmdStatus
      } authorisation_status;

      enum
      {
         CheckConnectionOperation,
         DownloadMessagesOperation
      };
      int current_operation;

      unsigned long total_nb_message;
      unsigned long current_message_id;

      wxEmailMessage::MessageId handled_message_id;

      bool PerformOperation(int op, Listener* operation_listener);

      unsigned long timeout_value;

      Listener::ExtractionMode_t extraction_mode;

      bool in_init;

      AuthenticationScheme_t authentication_scheme;
};


#endif // _WX_POP3_H_
