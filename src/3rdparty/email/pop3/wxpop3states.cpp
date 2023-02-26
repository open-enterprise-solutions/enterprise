////////////////////////////////////////////////////////////////////////////
// Name:        wxpop3states.cpp
// Purpose:     This class implements the different states of the pop3 client FSM
// Author:      Brice André
// Created:     2010/12/05
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

#include <3rdparty/email/pop3/wxpop3.h>
#include <3rdparty/email/utils/wxmd5.hpp>

void wxPOP3::InitialState::onEnterState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Entering InitialState\n");

	/* Perform the server connection */
	context.Connect();

	/* check if we shall perform a ssl connection */
	if (((wxPOP3&)context).ssl_enabled)
	{
		context.ChangeState(wxPOP3::ssl_negociation_state);
	}
	else
	{
		context.ChangeState(wxPOP3::hello_state);
	}
}

void wxPOP3::InitialState::onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const
{
	WX_SMTP_PRINT_DEBUG("Leaving InitialState\n");
}

void wxPOP3::InitialState::onDisconnect(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::InitialState::onTimeout(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::InitialState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
	/* check the response content */
	if (line.StartsWith(_T("+OK")))
	{
		/* extract digest that will be used during APOP command */
		((wxPOP3&)context).session_digest = wxString(_T("<")) + line.AfterFirst('<').BeforeLast('>');

		/* Check if we shall switch to ssl or authorisation state */
		if (((wxPOP3&)context).ssl_enabled)
		{
			context.ChangeState(wxPOP3::ssl_negociation_state);
		}
		else
		{
			context.ChangeState(wxPOP3::hello_state);
		}
	}
	else
	{
		((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
		context.ChangeState(wxPOP3::exit_state);
	}
}

void wxPOP3::SslNegociationState::onEnterState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Entering SslNegociationState\n");

	/* Send the HELLO command */
	switch (context.InitiateSSLSession())
	{
	case SslConnected:
		context.ChangeState(wxPOP3::hello_state);
		break;

	case SslPending:
		context.TimerStart(((wxPOP3&)context).timeout_value);
		break;

	case SslFailed:
	default:
		((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
		context.ChangeState(wxPOP3::exit_state);
		break;
	}
}

void wxPOP3::SslNegociationState::onLeaveState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Leaving SslNegociationState\n");
	context.TimerStop();
}

void wxPOP3::SslNegociationState::onResponse(wxCmdlineProtocol& context, const wxString& WXUNUSED(line)) const
{
	/* try a new connection */
	switch (context.InitiateSSLSession())
	{
	case SslConnected:
		context.ChangeState(wxPOP3::hello_state);
		break;

	case SslPending:
		context.TimerStart(((wxPOP3&)context).timeout_value);
		break;

	case SslFailed:
	default:
		((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
		context.ChangeState(wxPOP3::exit_state);
		break;
	}
}

void wxPOP3::SslNegociationState::onDisconnect(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::SslNegociationState::onTimeout(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::HelloState::onEnterState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Entering HelloState\n");

	/* Start timer */
	context.TimerStart(((wxPOP3&)context).timeout_value);
}

void wxPOP3::HelloState::onLeaveState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Leaving HelloState\n");
	context.TimerStop();
}

void wxPOP3::HelloState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
	/* check the response content */
	if (line.StartsWith(_T("+OK")))
	{
		/* extract digest that will be used during APOP command */
		((wxPOP3&)context).session_digest = wxString(_T("<")) + line.AfterFirst('<').BeforeLast('>');

		/* Switch to authorisation state */
		context.ChangeState(wxPOP3::authorisation_state);
	}
	else
	{
		((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
		context.ChangeState(wxPOP3::exit_state);
	}
}

void wxPOP3::HelloState::onDisconnect(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::HelloState::onTimeout(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::AuthorisationState::onEnterState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Entering AuthorisationState\n");

	/* Initiate the APOP login command */

	/* compute digest */
	wxString digest = wxMD5::ComputeMd5(((wxPOP3&)context).session_digest + ((wxPOP3&)context).user_password);

	/* check which authentication scheme shall be used */
	switch (((wxPOP3&)context).authentication_scheme)
	{
	case UserPassAuthenticationMethod:

		/* Send the USER command command */
		context.SendLine(wxString(_T("USER ")) + ((wxPOP3&)context).user_name);

		/* Initialize the authorisation state */
		((wxPOP3&)context).authorisation_status = wxPOP3::SentUserCmdStatus;

		break;

	case APopAuthenticationMethod:
	default:

		/* Send the APOP command */
		context.SendLine(wxString(_T("APOP ")) + ((wxPOP3&)context).user_name + _T(" ") + digest);

		/* Initialize the authorisation state */
		((wxPOP3&)context).authorisation_status = wxPOP3::SentApopCmdStatus;

		break;
	}

	/* Start timeout */
	context.TimerStart(((wxPOP3&)context).timeout_value);
}

void wxPOP3::AuthorisationState::onConnect(wxCmdlineProtocol& context) const
{
	/* Should never occur... */
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::AuthorisationState::onDisconnect(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::AuthorisationState::onTimeout(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::AuthorisationState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
	if (line.StartsWith(_T("+OK")))
	{
		switch (((wxPOP3&)context).authorisation_status)
		{
		case SentUserCmdStatus:

			/* We shall send the PASS command */
			context.SendLine(wxString(_T("PASS ")) + ((wxPOP3&)context).user_password);
			((wxPOP3&)context).authorisation_status = wxPOP3::SentPassCmdStatus;
			break;

		case SentApopCmdStatus:
		case SentPassCmdStatus:

			/* Authentication phase succeeded */
			switch (((wxPOP3&)context).current_operation)
			{
			case wxPOP3::CheckConnectionOperation:
				((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Succeed);
				context.ChangeState(wxPOP3::exit_state);
				break;

			case wxPOP3::DownloadMessagesOperation:
				context.ChangeState(wxPOP3::get_message_list_state);
				break;

			default:
				/* Should never occur... */
				((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
				context.ChangeState(wxPOP3::exit_state);
				break;
			}
			break;

		default:
			/* Should never occur... */
			((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
			context.ChangeState(wxPOP3::exit_state);
			break;
		}
	}
	else
	{
		/* check in which mode we are */
		switch (((wxPOP3&)context).authorisation_status)
		{
		case SentApopCmdStatus:

			/* Apop can fail if no present -> switch to USER/PASS command */
			context.SendLine(wxString(_T("USER ")) + ((wxPOP3&)context).user_name);
			((wxPOP3&)context).authorisation_status = wxPOP3::SentUserCmdStatus;
			break;

		case SentPassCmdStatus:

			/* Probably invalid password provided */
			((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::InvalidUserPass);
			context.ChangeState(wxPOP3::exit_state);
			break;

		case SentUserCmdStatus:
		default:

			/* Problem occured*/
			((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
			context.ChangeState(wxPOP3::exit_state);
			break;
		}
	}
}

void wxPOP3::AuthorisationState::onLeaveState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Leaving AuthorisationState\n");

	context.TimerStop();
}

void wxPOP3::GetMessagesListState::onConnect(wxCmdlineProtocol& context) const
{
	/* Should never occur... */
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::GetMessagesListState::onDisconnect(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::GetMessagesListState::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
	/* check type of answer */
	unsigned long msg_nr;
	if (line.StartsWith(_T("+OK")))
	{
		/* refresh timer */
		context.TimerRestart();
	}
	else if (line == _T("."))
	{
		/* We received complete list -> propose user to filter handled messages */
		((wxPOP3&)context).current_operation_listener->OnFilterHandledMessages(((wxPOP3&)context).messages_list);

		/* switch to HandlingMessage */
		context.ChangeState(wxPOP3::handling_message_state);
	}
	else if (line.BeforeFirst(' ').ToULong(&msg_nr))
	{
		/* Append message to list */
		if (((wxPOP3&)context).in_uid_list)
		{
			((wxPOP3&)context).messages_list.emplace_back(wxEmailMessage::MessageId(true, line.AfterFirst(' '), (void*)msg_nr));
		}
		else
		{
			((wxPOP3&)context).messages_list.emplace_back(wxEmailMessage::MessageId(false, line.BeforeFirst(' '), (void*)msg_nr));
		}

		/* refresh timer */
		context.TimerRestart();
	}
	else
	{
		/* An error occured -> check mode in which we are */
		if (((wxPOP3&)context).in_uid_list && ((wxPOP3&)context).messages_list.size() <= 0)
		{
			/* UIDL command probably not handled -> we will switch to LIST mode */
			context.SendLine(_T("LIST"));
			((wxPOP3&)context).in_uid_list = false;
		}
		else
		{
			/* Error occured -> exit... */
			((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
			context.ChangeState(wxPOP3::exit_state);
		}
	}
}

void wxPOP3::GetMessagesListState::onTimeout(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::GetMessagesListState::onEnterState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Entering GetMessagesListState\n");

	/* Send the LIST command */
	context.SendLine(_T("UIDL"));

	/* Clears the list_message stuff */
	((wxPOP3&)context).messages_list.clear();
	((wxPOP3&)context).in_uid_list = true;

	/* Start timer */
	context.TimerStart(((wxPOP3&)context).timeout_value);
}

void wxPOP3::GetMessagesListState::onLeaveState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Leaving GetMessagesListState\n");

	context.TimerStop();

	/* Update messages stats */
	((wxPOP3&)context).total_nb_message = ((wxPOP3&)context).messages_list.size();
	((wxPOP3&)context).current_message_id = 0;
}

void wxPOP3::HandlingMessage::onConnect(wxCmdlineProtocol& context) const
{
	/* This state is no more useful -> should be suppressed */
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::HandlingMessage::onDisconnect(wxCmdlineProtocol& context) const
{
	/* This state is no more useful -> should be suppressed */
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::HandlingMessage::onResponse(wxCmdlineProtocol& context, const wxString& WXUNUSED(line)) const
{
	/* This state is no more useful -> should be suppressed */
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::HandlingMessage::onTimeout(wxCmdlineProtocol& context) const
{
	/* This state is no more useful -> should be suppressed */
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::HandlingMessage::onEnterState(wxCmdlineProtocol& context) const
{
	/* This state is no more useful -> should be suppressed */
	WX_SMTP_PRINT_DEBUG("Entering HandlingMessage\n");

	/* Check if there is a message to send */
	if (((wxPOP3&)context).messages_list.size() <= 0)
	{
		/* go to exit state */
		((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Succeed);
		context.ChangeState(wxPOP3::exit_state);
		return;
	}

	/* Prepare the message id */
	((wxPOP3&)context).handled_message_id = ((wxPOP3&)context).messages_list.front();

	/* Invoke user callback */
	Listener::ExtractionMode_t extraction_mode;
	bool shall_delete, shall_stop;
	((wxPOP3&)context).current_message_id++;
	((wxPOP3&)context).current_operation_listener->OnFoundMessage(((wxPOP3&)context).handled_message_id,
		((wxPOP3&)context).current_message_id,
		((wxPOP3&)context).total_nb_message,
		extraction_mode,
		shall_delete,
		shall_stop);

	/* Check what the user requested */
	if (shall_stop)
	{
		((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Abort);
		context.ChangeState(wxPOP3::exit_state);
	}
	else if (extraction_mode != Listener::NoExtraction)
	{
		((wxPOP3&)context).extraction_mode = extraction_mode;
		((wxPOP3&)context).shall_suppress = shall_delete;
		context.ChangeState(wxPOP3::downloading_message_state);
	}
	else if (shall_delete)
	{
		context.ChangeState(wxPOP3::suppressing_message_state);
	}
	else
	{
		/* Suppress this message an re-enter state */
		((wxPOP3&)context).messages_list.pop_front();
		context.ChangeState(wxPOP3::handling_message_state);
	}
}

void wxPOP3::HandlingMessage::onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const
{
	WX_SMTP_PRINT_DEBUG("Leaving HandlingMessage\n");
}

void wxPOP3::DownloadingMessage::onConnect(wxCmdlineProtocol& context) const
{
	/* Should never occur... */
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::DownloadingMessage::onDisconnect(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::DownloadingMessage::onTimeout(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::DownloadingMessage::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
	/* check which type of answer we get */
	if (line.StartsWith(_T("+OK")))
	{
		/* Restart timer */
		context.TimerRestart();
	}
	else if (line.StartsWith(_T("-ERR")))
	{
		/* If we are in a TOP command, we can accept this, as this is an optional command */
		if (((wxPOP3&)context).extraction_mode == Listener::ExtractHeader)
		{
			/* Switch to extract full message and re-enter state */
			((wxPOP3&)context).extraction_mode = Listener::ExtractFullMessage;
			context.ChangeState(wxPOP3::downloading_message_state);
		}
		else
		{
			/* Switch to error state */
			((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
			context.ChangeState(wxPOP3::exit_state);
		}
	}
	else if (line == _T("."))
	{
		/* End of message -> convert it */

		/* Get the corresponding message */
		wxEmailMessage* message =
			new wxEmailMessage(((wxPOP3&)context).message_content,
			((wxPOP3&)context).handled_message_id,
				((wxPOP3&)context).extraction_mode == Listener::ExtractFullMessage);

		/* Trigger callback */
		bool shall_stop = false;
		((wxPOP3&)context).current_operation_listener->OnMessageContent(message, shall_stop);

		if (shall_stop)
		{
			((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Abort);
			context.ChangeState(wxPOP3::exit_state);
		}
		else if (((wxPOP3&)context).shall_suppress)
		{
			context.ChangeState(wxPOP3::suppressing_message_state);
		}
		else
		{
			/* Remove message from list */
			((wxPOP3&)context).messages_list.pop_front();

			/* Jump to handling message */
			context.ChangeState(wxPOP3::handling_message_state);
		}
	}
	else
	{
		/* Append content to line */
		if (((wxPOP3&)context).message_content != _T(""))
		{
			((wxPOP3&)context).message_content += _T("\r\n");
		}
		if (line.StartsWith(_T("..")))
		{
			((wxPOP3&)context).message_content += line.AfterFirst('.');
		}
		else
		{
			((wxPOP3&)context).message_content += line;
		}

		/* Restat timer */
		context.TimerRestart();
	}
}

void wxPOP3::DownloadingMessage::onEnterState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Entering DownloadingMessage\n");

	/* Reset message content */
	((wxPOP3&)context).message_content = _T("");

	/* Initiate the RETR command */
	if (((wxPOP3&)context).extraction_mode == Listener::ExtractHeader)
	{
		//TODO : why 30 lines ?
		context.SendLine(wxString(_T("TOP ")) << (long)((wxPOP3&)context).messages_list.front().user_data << _T(" 30"));
	}
	else
	{
		context.SendLine(wxString(_T("RETR ")) << (long)((wxPOP3&)context).messages_list.front().user_data);
	}

	/* Start timer */
	context.TimerStart(((wxPOP3&)context).timeout_value);
}

void wxPOP3::DownloadingMessage::onLeaveState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Leaving DownloadingMessage\n");

	/* Stop timer */
	context.TimerStop();
}

void wxPOP3::SuppressingMessage::onConnect(wxCmdlineProtocol& context) const
{
	/* Should never occur... */
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::SuppressingMessage::onDisconnect(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Error);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::SuppressingMessage::onTimeout(wxCmdlineProtocol& context) const
{
	((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
	context.ChangeState(wxPOP3::exit_state);
}

void wxPOP3::SuppressingMessage::onResponse(wxCmdlineProtocol& context, const wxString& line) const
{
	if (line.StartsWith(_T("+OK")))
	{
		((wxPOP3&)context).messages_list.pop_front();
		context.ChangeState(wxPOP3::handling_message_state);
	}
	else
	{
		((wxPOP3&)context).current_operation_listener->OnOperationFinished(wxPOP3::Listener::Timeout);
		context.ChangeState(wxPOP3::exit_state);
	}
}

void wxPOP3::SuppressingMessage::onEnterState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Entering SuppressingMessage\n");

	/* Initiate the DELE message */
	context.SendLine(wxString(_T("DELE ")) << (long)((wxPOP3&)context).messages_list.front().user_data);

	/* Start timer */
	context.TimerStart(((wxPOP3&)context).timeout_value);
}

void wxPOP3::SuppressingMessage::onLeaveState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Leaving SuppressingMessage\n");

	context.TimerStop();
}

void wxPOP3::ExitState::onEnterState(wxCmdlineProtocol& context) const
{
	WX_SMTP_PRINT_DEBUG("Entering ExitState\n");

	/* Just hanlde special case where we enter this state at init */
	if (!((wxPOP3&)context).in_init)
	{
		context.SendLine(_T("QUIT"));
	}
}

void wxPOP3::ExitState::onDisconnect(wxCmdlineProtocol& context) const
{
	context.Disconnect();
}

void wxPOP3::ExitState::onResponse(wxCmdlineProtocol& context, const wxString& WXUNUSED(line)) const
{
	context.Disconnect();
}

void wxPOP3::ExitState::onTimeout(wxCmdlineProtocol& context) const
{
	context.Disconnect();
}
