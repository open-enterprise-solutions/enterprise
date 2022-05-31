////////////////////////////////////////////////////////////////////////////
// Name:        wxSSLSocketClient.cpp
// Purpose:     This class implements the wxSSLSocketClient
// Author:      Brice Andr�
// Created:     2011/01/25
// RCS-ID:      $Id: mycomp.cpp 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2011 Brice Andr�
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////

#ifdef OPEN_SSL_STATIC_LINK
#define CALL_SSL(lib,func) func
#else
#define CALL_SSL(lib,func) lib.func
#endif

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

#include <email/ssl/wxSSLSocketClient.h>

wxSSLSocketClient::OpenSslInitialisator::OpenSslInitialisator()
{
	/* We perform a lazy load of lib so that it does not fail if openssl lib could not be loaded in case it is not requested */
	lib_is_initialised = false;
}

wxSSLSocketClient::OpenSslInitialisator::~OpenSslInitialisator()
{

}

const wxSSLSocketClient::OpenSslInitialisator::LibIf_t& wxSSLSocketClient::OpenSslInitialisator::GetLibIf()
{
	/* Check if lib was already loaded */
	if (!lib_is_initialised)
	{
#ifndef OPEN_SSL_STATIC_LINK
		/* load the library */
		if (!ssl_lib.Load(wxT("ssleay32")))
		{
			throw Exception(wxT("Unable to load ssleay32 dynamic library"));
		}
		if (!lib_eay.Load(wxT("libeay32")))
		{
			throw Exception(wxT("Unable to load libeay32 dynamic library"));
		}

		/* Initialize the library */
		((void(*)(void))LoadSymbol(ssl_lib, wxT("SSL_load_error_strings"), wxT("ssleay32")))();
		((void(*)(void))LoadSymbol(ssl_lib, wxT("SSL_library_init"), wxT("ssleay32")))();

		/* Initialize all requested symbols */
		lib_if.SSLv23_client_method = (const SSL_METHOD*(*)(void))LoadSymbol(ssl_lib, wxT("SSLv23_client_method"), wxT("ssleay32"));
		lib_if.SSL_CTX_new = (SSL_CTX*(*)(const SSL_METHOD*))LoadSymbol(ssl_lib, wxT("SSL_CTX_new"), wxT("ssleay32"));
		lib_if.SSL_CTX_free = (void(*)(SSL_CTX*))LoadSymbol(ssl_lib, wxT("SSL_CTX_free"), wxT("ssleay32"));
		lib_if.SSL_CTX_ctrl = (long(*)(SSL_CTX*, int, long, void*))LoadSymbol(ssl_lib, wxT("SSL_CTX_ctrl"), wxT("ssleay32"));
		lib_if.SSL_new = (SSL*(*)(SSL_CTX*))LoadSymbol(ssl_lib, wxT("SSL_new"), wxT("ssleay32"));
		lib_if.SSL_free = (void(*)(SSL*))LoadSymbol(ssl_lib, wxT("SSL_free"), wxT("ssleay32"));
		lib_if.SSL_set_cipher_list = (int(*)(SSL*, const char*))LoadSymbol(ssl_lib, wxT("SSL_set_cipher_list"), wxT("ssleay32"));
		lib_if.SSL_set_bio = (void(*)(SSL*, BIO*, BIO*))LoadSymbol(ssl_lib, wxT("SSL_set_bio"), wxT("ssleay32"));
		lib_if.SSL_set_connect_state = (void(*)(SSL*))LoadSymbol(ssl_lib, wxT("SSL_set_connect_state"), wxT("ssleay32"));
		lib_if.SSL_connect = (int(*)(SSL*))LoadSymbol(ssl_lib, wxT("SSL_connect"), wxT("ssleay32"));
		lib_if.SSL_shutdown = (int(*)(SSL*))LoadSymbol(ssl_lib, wxT("SSL_shutdown"), wxT("ssleay32"));
		lib_if.SSL_read = (int(*)(SSL*, void*, int))LoadSymbol(ssl_lib, wxT("SSL_read"), wxT("ssleay32"));
		lib_if.SSL_write = (int(*)(SSL*, const void*, int))LoadSymbol(ssl_lib, wxT("SSL_write"), wxT("ssleay32"));
		lib_if.SSL_get_error = (int(*)(const SSL *ssl, int ret))LoadSymbol(ssl_lib, wxT("SSL_get_error"), wxT("ssleay32"));

		lib_if.BIO_new = (BIO*(*)(BIO_METHOD*))LoadSymbol(lib_eay, wxT("BIO_new"), wxT("libeay32"));
		lib_if.BIO_free = (int(*)(BIO*))LoadSymbol(lib_eay, wxT("BIO_free"), wxT("libeay32"));
		lib_if.ERR_get_error = (unsigned long(*)(void))LoadSymbol(lib_eay, wxT("ERR_get_error"), wxT("libeay32"));
		lib_if.ERR_error_string = (char*(*)(unsigned long e, char *buf))LoadSymbol(lib_eay, wxT("ERR_error_string"), wxT("libeay32"));
#else
		SSL_load_error_strings();
		SSL_library_init();
#endif
		lib_is_initialised = true;

	}
	return lib_if;
}

void* wxSSLSocketClient::OpenSslInitialisator::LoadSymbol(wxDynamicLibrary& lib, const wxString& name, const wxString& lib_name)
{
	void* result = lib.GetSymbol(name);
	if (result == NULL)
	{
		throw Exception(wxString::Format(_("Unable to load symbol \"%s\" from library \"%s\""),
			name.fn_str(),
			lib_name.fn_str()));
	}
	return result;
}

wxSSLSocketClient::wxSSLSocketClient(wxSocketFlags flags)
	:wxSocketClient(flags),
	connection_state(NoSslConnectionState)
{
	/* configure internal stuff */
	ctx = NULL;
	ssl = NULL;
	bio = NULL;

	last_ssl_count = 0;
	ssl_error = true;
}

wxSSLSocketClient::~wxSSLSocketClient()
{
	/* Check if we are currently in SSL connection */
	if (connection_state != NoSslConnectionState)
	{
		/* We shall first switch to blocking mode because, if object is destroyed during
		 * wxWidgets cleanup, no more event loop is running
		 */
		 //TODO : note that if we detect that we are in cleanup phase, we could avoid configuring socket in
		 // blocking mode when not requested...
		SetFlags(wxSOCKET_WAITALL | wxSOCKET_BLOCK);

		/* Now, we can safely cleanup socket stuff */
		TerminateSSLSession();
	}
}

wxSSLSocketClient::SslSessionStatus_t wxSSLSocketClient::InitiateSSLSession()
{
	/* Check if we are not already in SSL session */
	if (connection_state == SslConnectedState)
	{
		/* We are already in SSL session... */
		return SslFailed;
	}

	/* Get lib info */
	const OpenSslInitialisator::LibIf_t& ssl_lib = open_ssl_initialisator.GetLibIf();

	/* Check if connection is pending */
	if (connection_state == NoSslConnectionState)
	{
		/* Configure ssl context */
		ctx = CALL_SSL(ssl_lib, SSL_CTX_new)(CALL_SSL(ssl_lib, SSLv23_client_method)());
		//ssl_lib.SSL_CTX_ctrl(ctx,SSL_CTRL_OPTIONS,SSL_OP_ALL,NULL);

		/* Create SSL connection */
		ssl = CALL_SSL(ssl_lib, SSL_new)(ctx);
		if (ssl != NULL)
		{
			/* Configure cypher algorithms supported */
			CALL_SSL(ssl_lib, SSL_set_cipher_list)(ssl, "ALL");

			bio = CALL_SSL(ssl_lib, BIO_new)(SslBio::GetBio());
			bio->ptr = this;
			CALL_SSL(ssl_lib, SSL_set_bio)(ssl, bio, bio);

			CALL_SSL(ssl_lib, SSL_set_connect_state)(ssl);
		}
		else
		{
			/* Release context */
			CALL_SSL(ssl_lib, SSL_CTX_free)(ctx);
			ctx = NULL;

			/* Return error status */
			return SslFailed;
		}
	}

	/* Try connection */
	int ret = CALL_SSL(ssl_lib, SSL_connect)(ssl);
	if (ret <= 0)
	{
		/* check if we are just in a retry mode */
		int error = CALL_SSL(ssl_lib, SSL_get_error)(ssl, ret);
		if ((error == SSL_ERROR_WANT_READ) ||
			(error == SSL_ERROR_WANT_WRITE))
		{
			/* mark connection as pending */
			connection_state = SsConnectionPendingState;
			return SslPending;
		}
		else
		{
			/* Release connection*/
			CALL_SSL(ssl_lib, SSL_free)(ssl);
			ssl = NULL;

			/* Release context */
			CALL_SSL(ssl_lib, SSL_CTX_free)(ctx);
			ctx = NULL;

			/* reset connection state */
			connection_state = NoSslConnectionState;
			return SslFailed;
		}
	}

	/* We are properly connected */
	connection_state = SslConnectedState;
	return SslConnected;
}

bool wxSSLSocketClient::TerminateSSLSession()
{
	/* check if we are connected */
	if (connection_state == NoSslConnectionState)
	{
		return false;
	}

	/* Get lib info */
	const OpenSslInitialisator::LibIf_t& ssl_lib = open_ssl_initialisator.GetLibIf();

	/* Close session */
	CALL_SSL(ssl_lib, SSL_shutdown)(ssl);

	/* Release connection*/
	CALL_SSL(ssl_lib, SSL_free)(ssl);
	ssl = NULL;

	/* Release context */
	CALL_SSL(ssl_lib, SSL_CTX_free)(ctx);
	ctx = NULL;

	return true;
}

bool wxSSLSocketClient::IsInSSLSession() const
{
	return connection_state == SslConnectedState;
}

bool wxSSLSocketClient::SslSessionPending() const
{
	return connection_state == SsConnectionPendingState;
}

wxSSLSocketClient& wxSSLSocketClient::Read(void* buffer, wxUint32 nbytes)
{
	/* check if we are in ssl mode */
	if (connection_state == SslConnectedState)
	{
		/* Perform a SSL read */
		int nb_bytes_read = CALL_SSL(open_ssl_initialisator.GetLibIf(), SSL_read)(ssl, buffer, nbytes);
		if (nb_bytes_read < 0)
		{
			last_ssl_count = 0;
			ssl_error = true;
		}
		else
		{
			last_ssl_count = nb_bytes_read;
			ssl_error = false;
		}
	}
	else if (connection_state == NoSslConnectionState)
	{
		/* perform a normal read */
		wxSocketClient::Read(buffer, nbytes);
	}
	else
	{
		last_ssl_count = 0;
		ssl_error = true;
	}

	return *this;
}

wxSSLSocketClient& wxSSLSocketClient::Write(const void *buffer, wxUint32 nbytes)
{
	/* check if we are in ssl mode */
	if (connection_state == SslConnectedState)
	{
		/* Perform a SSL read */
		int nb_bytes_written = CALL_SSL(open_ssl_initialisator.GetLibIf(), SSL_write)(ssl, buffer, nbytes);
		if (nb_bytes_written < 0)
		{
			last_ssl_count = 0;
			ssl_error = true;
		}
		else
		{
			last_ssl_count = nb_bytes_written;
			ssl_error = false;
		}
	}
	else if (connection_state == NoSslConnectionState)
	{
		/* perform a normal read */
		wxSocketClient::Write(buffer, nbytes);
	}
	else
	{
		last_ssl_count = 0;
		ssl_error = true;
	}

	return *this;
}

wxSSLSocketClient& wxSSLSocketClient::BasicRead(void* buffer, wxUint32 nbytes)
{
	return (wxSSLSocketClient&)wxSocketClient::Read(buffer, nbytes);
}

wxSSLSocketClient& wxSSLSocketClient::BasicWrite(const void *buffer, wxUint32 nbytes)
{
	return (wxSSLSocketClient&)wxSocketClient::Write(buffer, nbytes);
}

wxUint32 wxSSLSocketClient::LastCount() const
{
	if (connection_state != NoSslConnectionState)
	{
		return last_ssl_count;
	}
	else
	{
		return wxSocketClient::LastCount();
	}
}

wxUint32 wxSSLSocketClient::BasicLastCount() const
{
	return wxSocketClient::LastCount();
}

bool wxSSLSocketClient::Error() const
{
	if (connection_state != NoSslConnectionState)
	{
		return ssl_error;
	}
	else
	{
		return wxSocketClient::Error();
	}
}

bool wxSSLSocketClient::BasicError() const
{
	return wxSocketClient::Error();
}

bool wxSSLSocketClient::Close()
{
	if (IsInSSLSession())
	{
		TerminateSSLSession();
	}
	return wxSocketClient::Close();
}

BIO_METHOD* wxSSLSocketClient::SslBio::GetBio()
{
	static BIO_METHOD methods =
	{
	   BIO_TYPE_SOCKET,
	   "wxEmail Socket",
	   Write,
	   Read,
	   Puts,
	   Gets,
	   Ctrl,
	   Create,
	   Destroy,
	   NULL,
	};
	return &methods;
}

int wxSSLSocketClient::SslBio::Write(BIO* bio, const char* buffer, int nbytes)
{
	wxSSLSocketClient* ptr = (wxSSLSocketClient*)bio->ptr;
	ptr->BasicWrite(buffer, nbytes);
	if (ptr->BasicError())
	{
		return -1;
	}
	else
	{
		return ptr->BasicLastCount();
	}
}

int wxSSLSocketClient::SslBio::Read(BIO* bio, char* buffer, int nbytes)
{
	wxSSLSocketClient* ptr = (wxSSLSocketClient*)bio->ptr;
	ptr->BasicRead(buffer, nbytes);
	if (ptr->BasicError())
	{
		return -1;
	}
	else
	{
		return ptr->BasicLastCount();
	}
}

int wxSSLSocketClient::SslBio::Puts(BIO*, const char*)
{
	throw Exception(wxT("wxSSLSocketClient::SslBio::Puts : not implemented"));
}

int wxSSLSocketClient::SslBio::Gets(BIO*, char*, int)
{
	throw Exception(wxT("wxSSLSocketClient::SslBio::Gets : not implemented"));
}

long wxSSLSocketClient::SslBio::Ctrl(BIO* WXUNUSED(bio), int WXUNUSED(ctrl), long WXUNUSED(arg), void* WXUNUSED(clbk))
{
	return 1;
}

int wxSSLSocketClient::SslBio::Create(BIO* bio)
{
	bio->init = 1;
	bio->num = -1;
	bio->ptr = NULL;
	bio->flags = 0;
	return 1;
}

int wxSSLSocketClient::SslBio::Destroy(BIO* bio)
{
	if (bio == NULL)
	{
		return 0;
	}
	if (bio->shutdown)
	{
		bio->init = 0;
		bio->flags = 0;
	}
	return 1;
}

wxSSLSocketClient::OpenSslInitialisator wxSSLSocketClient::open_ssl_initialisator;
