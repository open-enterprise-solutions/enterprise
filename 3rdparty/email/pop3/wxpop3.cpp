////////////////////////////////////////////////////////////////////////////
// Name:        wxpop3.cpp
// Purpose:     This class implements the pop3 client
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
#include <wx/wx.h>
#endif

#include <email/pop3/wxpop3.h>

#define DEFAULT_TIMEOUT_VALUE 60

wxPOP3::wxPOP3(wxString client_name,
               wxString client_password,
               wxString server_name,
               unsigned long port,
               AuthenticationScheme_t authentication_scheme,
               bool ssl_enabled,
               Listener* default_listener)
      :default_instance_listener(default_listener),
       user_name(client_name),
       user_password(client_password),
       in_init(true),
       ssl_enabled(ssl_enabled),
       authentication_scheme(authentication_scheme)
{
   timeout_value = DEFAULT_TIMEOUT_VALUE;

   SetHost(server_name, port);

   if (default_instance_listener == NULL)
   {
      default_instance_listener = &default_class_listener;
   }
   
   ChangeState(exit_state);

   in_init = false;
}

void wxPOP3::ConfigureAuthenticationScheme(AuthenticationScheme_t authentication_scheme,
                                           const wxString& user_name,
                                           const wxString& password,
                                           bool ssl_enabled)
{
   this->authentication_scheme = authentication_scheme;
   this->user_name = user_name;
   this->user_password = password;
   this->ssl_enabled = ssl_enabled;
}

bool wxPOP3::OperationInProgress()
{
   return (&GetCurrentState()) != &exit_state;
}

bool wxPOP3::CheckConnection(Listener* operation_listener)
{
   return PerformOperation(CheckConnectionOperation, operation_listener);
}

bool wxPOP3::DownloadMessages(Listener* operation_listener)
{
   return PerformOperation(DownloadMessagesOperation, operation_listener);
}

bool wxPOP3::PerformOperation(int op, Listener* operation_listener)
{
   /* Check if an operation is in progress */
    if (OperationInProgress())
    {
       return false;
    }

    /* Assign default listener */
    if (operation_listener)
    {
       this->current_operation_listener = operation_listener;
    }
    else
    {
       this->current_operation_listener = default_instance_listener;
    }

    /* Configure operation */
    current_operation = op;

    /* Enter connection state */
    ChangeState(initial_state);

    return true;
}

wxPOP3::InitialState           wxPOP3::initial_state;
wxPOP3::SslNegociationState    wxPOP3::ssl_negociation_state;
wxPOP3::HelloState             wxPOP3::hello_state;
wxPOP3::AuthorisationState     wxPOP3::authorisation_state;
wxPOP3::GetMessagesListState   wxPOP3::get_message_list_state;
wxPOP3::HandlingMessage        wxPOP3::handling_message_state;
wxPOP3::DownloadingMessage     wxPOP3::downloading_message_state;
wxPOP3::SuppressingMessage     wxPOP3::suppressing_message_state;
wxPOP3::ExitState              wxPOP3::exit_state;

wxPOP3::Listener               wxPOP3::default_class_listener;
