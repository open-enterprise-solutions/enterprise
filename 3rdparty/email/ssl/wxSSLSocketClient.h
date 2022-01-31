/////////////////////////////////////////////////////////////////////////////
// Name:        wxSSLSocketClient.h
// Purpose:     This class is a wrapper around cyassl library for use with wxSidgets.
// Author:      Brice André
// Created:     2011/01/25
// RCS-ID:      $Id: mycomp.h 505 2007-03-31 10:31:46Z frm $
// Copyright:   (c) 2011 Brice André
// Licence:     wxWidgets licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_SSL_SOCKET_CLIENT_H_
#define _WX_SSL_SOCKET_CLIENT_H_

// wxWidgets headers
#include <wx/wx.h>

#include <wx/socket.h>
#include <wx/dynlib.h>

/* Enable this flag if you wish to perform a static link with openssl -> for debug purpose only... */
//#define OPEN_SSL_STATIC_LINK

/* Perform include stuff */
#ifdef OPEN_SSL_STATIC_LINK

#else
#include <email/ssl/wxOpenSSL.h>
#endif


/*!
 * \internal
 *
 * This class is a wrapper arount the OpenSSL library for use with wxWidgets.
 *
 * Note that it only performs a partial implementation of SSL functionalities and
 * its semantics is slightly different from the wxSocketClient one.
 *
 * This class allows dialoging with a server requesting an SSL connection, but it will
 * not use any certificate. As a result, the identity of the server cannot be checked and
 * the server will not be able to check the identity of the client.
 *
 * The wxSocketBase class does not define the different functions as virtual. As a result,
 * this class will not behave correctly if a pointer downcasted to wxSocketClient or
 * wxSocketBase is used.
 *
 * The events can be used to be notified when data is available or when data is ready to
 * be sent. But those events only concern low-level data. So, for example, when a wxSOCKET_INPUT
 * event is issued, it does not mean that a complete SSL message has been received. So, a blocking
 * read operation could block, and a non-blocking one could return nothing. The same applies to
 * WaitForRead operation. Note that, even if the Read operation does not succeed, the input buffer is
 * emptied. So, after a read returned nothing, WaitForRead will block until new data is available.
 */
class wxSSLSocketClient : public wxSocketClient
{
public:

	class Exception
	{
	public:
		Exception(const wxString& message) :message(message) {}
		const wxString& GetMessage() const;
	private:
		const wxString message;
	};

	wxSSLSocketClient(wxSocketFlags flags = wxSOCKET_NONE);
	~wxSSLSocketClient();

	typedef enum
	{
		SslConnected,
		SslFailed,
		SslPending
	} SslSessionStatus_t;
	SslSessionStatus_t InitiateSSLSession();
	bool TerminateSSLSession();
	bool IsInSSLSession() const;
	bool SslSessionPending() const;

	wxSSLSocketClient& Read(void* buffer, wxUint32 nbytes);
	wxSSLSocketClient& Write(const void *buffer, wxUint32 nbytes);

	wxUint32 LastCount() const;
	bool Error() const;

	virtual bool Close();

private:

	wxSSLSocketClient& BasicRead(void* buffer, wxUint32 nbytes);
	wxSSLSocketClient& BasicWrite(const void *buffer, wxUint32 nbytes);

	bool BasicError() const;
	wxUint32 BasicLastCount() const;

	typedef enum
	{
		NoSslConnectionState,
		SslConnectedState,
		SsConnectionPendingState
	} ConnectionState_t;
	ConnectionState_t connection_state;
	SSL_CTX* ctx;
	SSL*     ssl;
	BIO*     bio;
	wxUint32 last_ssl_count;
	bool     ssl_error;

	/*!
	 * This class is used to perform init anc cleanup of cyassl library.
	 */
	class OpenSslInitialisator
	{
	public:
		OpenSslInitialisator();
		~OpenSslInitialisator();

		typedef struct
		{
			const SSL_METHOD*(*SSLv23_client_method)(void);
			SSL_CTX*(*SSL_CTX_new)(const SSL_METHOD*);
			void(*SSL_CTX_free)(SSL_CTX*);
			long(*SSL_CTX_ctrl)(SSL_CTX*, int, long, void*);
			SSL*(*SSL_new)(SSL_CTX*);
			void(*SSL_free)(SSL*);
			int(*SSL_set_cipher_list)(SSL*, const char*);
			void(*SSL_set_bio)(SSL*, BIO*, BIO*);
			void(*SSL_set_connect_state)(SSL*);
			int(*SSL_connect)(SSL*);
			int(*SSL_shutdown)(SSL*);
			BIO*(*BIO_new)(BIO_METHOD*);
			int(*BIO_free)(BIO*);
			int(*SSL_read)(SSL*, void*, int);
			int(*SSL_write)(SSL*, const void*, int);
			int(*SSL_get_error)(const SSL *ssl, int ret);
			unsigned long(*ERR_get_error)(void);
			char*(*ERR_error_string)(unsigned long e, char *buf);
		} LibIf_t;
		const LibIf_t& GetLibIf();

	private:
		wxDynamicLibrary ssl_lib;
		wxDynamicLibrary lib_eay;
		bool lib_is_initialised;
		LibIf_t lib_if;

		void* LoadSymbol(wxDynamicLibrary& lib, const wxString& name, const wxString& lib_name);
	};
	static OpenSslInitialisator open_ssl_initialisator;

	class SslBio
	{
	public:
		static BIO_METHOD* GetBio();
	private:
		static int Write(BIO *, const char *, int);
		static int Read(BIO *, char *, int);
		static int Puts(BIO *, const char *);
		static int Gets(BIO *, char *, int);
		static long Ctrl(BIO *, int, long, void *);
		static int Create(BIO *);
		static int Destroy(BIO *);
	};
};

class wxSslSocketOutputStream : public wxOutputStream
{
public:
	wxSslSocketOutputStream(wxSSLSocketClient& s) :m_o_socket(s) {}
	virtual ~wxSslSocketOutputStream() {}

	wxFileOffset SeekO(wxFileOffset WXUNUSED(pos), wxSeekMode WXUNUSED(mode)) { return -1; }
	wxFileOffset TellO() const { return -1; }

protected:
	wxSSLSocketClient& m_o_socket;

	size_t OnSysWrite(const void *buffer, size_t size)
	{
		size_t ret = m_o_socket.Write((const char *)buffer, size).LastCount();
		m_lasterror = m_o_socket.Error() ? wxSTREAM_WRITE_ERROR : wxSTREAM_NO_ERROR;
		return ret;
	}

	DECLARE_NO_COPY_CLASS(wxSslSocketOutputStream)
};



#endif /* _WX_SSL_SOCKET_CLIENT_H_ */
