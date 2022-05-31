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
 $Log: wxcmdprot.cpp,v $
 Revision 1.7  2004/06/29 11:06:20  tavasti
 - Added OnResponse handler also for initial state (sometimes OnConnect
   arrives after first data)
 - Minor changes in indentation & comments

 Revision 1.6  2004/05/19 04:06:26  tavasti
 Fixes based on comments from Edwards John-BLUW23 <jedwards@motorola.com>
 - Removed -m486 flags from makefile
 - Added filetypes wav & mp3
 - Removed default arguments from wxmime.cpp (only in .h)
 - Commented out iostream.h includes

 Revision 1.5  2003/11/21 12:36:46  tavasti
 - Makefilet -Wall optioilla
 - Korjattu 'j‰rkev‰t' varoitukset pois (J‰‰nyt muutama joita ei saa
   kohtuudella poistettua)

 Revision 1.4  2003/11/14 15:43:09  tavasti
 Sending email with alternatives works

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

#include <email/utils/wxcmdprot.h>

static long SOCKET_ID = wxNewId();
static long TIMER_ID = wxNewId();

BEGIN_EVENT_TABLE(wxCmdlineProtocol, wxEvtHandler)
EVT_SOCKET(SOCKET_ID, wxCmdlineProtocol::OnSocketEvent)
EVT_TIMER(TIMER_ID, wxCmdlineProtocol::OnTimerEvent)
END_EVENT_TABLE()

wxCmdlineProtocol::wxCmdlineProtocol()
	:m_socket_usage_id(0),
	m_host(_T("")),
	m_port(0),
	m_inputLine(_T("")),
	timer(this, TIMER_ID),
	current_state(&default_state)
{}

wxCmdlineProtocol::~wxCmdlineProtocol()
{
	/* Just to be sure we will have no more event after destruction... */
	NextSocketSerialNr();
}

void wxCmdlineProtocol::Connect()
{
	if (IsConnected())
	{
		Disconnect();
	}

	//
	// I'm paranoid that we might not have called Close() in
	// some uncommon scenario where, for example, the server
	// goes away suddenly, so I'm not confident incserial()
	// got called between retries. It's OK to call it here
	// again.
	//
	NextSocketSerialNr();
	((wxSocketClient*)this)->SetClientData((void *)GetCurrentSocketSerialNr());

	SetTimeout(60);
	((wxSocketClient*)this)->SetEventHandler(*this, SOCKET_ID);
	SetNotify(wxSOCKET_CONNECTION_FLAG |
		wxSOCKET_INPUT_FLAG |
		wxSOCKET_LOST_FLAG);
	Notify(TRUE);

	// connect
	wxIPV4address addr;
	addr.Hostname(m_host);
	addr.Service(m_port);

	/*
	 * Perform connection, but do not wait server to answer...
	 * This will be catched by event
	 */
	wxSocketClient::Connect(addr, false);
}

void wxCmdlineProtocol::Disconnect()
{
	/* Just get next serial */
	NextSocketSerialNr();
}

void wxCmdlineProtocol::OnInput()
{
	/* Check if an SSL session is pending */
	if (SslSessionPending())
	{
		/* Perform a dummy input */
		WX_SMTP_PRINT_DEBUG("Ssl connection data");
		GetCurrentState().onResponse(*this, wxEmptyString);
	}
	else
	{
		while (1)
		{
			// get data
			const int bufsize = 256;
			char buf[bufsize];
			Read(buf, bufsize);
			if (!Error())
			{
				if (LastCount() > 0)
				{
					m_inputLine += wxString(buf, wxConvLocal, LastCount());
				}
				else
				{
					break;
				}
			}
			else
			{
				break;
			}


			// search for a newline
			size_t pos = 0;
			while (long(pos) < long(long(m_inputLine.Length()) - 1))
			{
				if (m_inputLine[pos] == 13)
				{
					if (m_inputLine[pos + 1] == 10)
					{
						// line found, evaluate
						WX_SMTP_PRINT_DEBUG("%s", m_inputLine.Mid(0, pos).mb_str(wxConvLocal));

						GetCurrentState().onResponse(*this, m_inputLine.Mid(0, pos));

						// adjust buffer
						m_inputLine = m_inputLine.Mid(pos + 2);

						/* restart search */
						pos = 0;
						continue;
					}
				}
				pos++;
			}
		}
	}
}

void wxCmdlineProtocol::OnSocketEvent(wxSocketEvent& event)
{
	if (event.GetClientData() == (void *)GetCurrentSocketSerialNr())
	{
		switch (event.GetSocketEvent())
		{
		case wxSOCKET_INPUT:
			OnInput();
			break;
		case wxSOCKET_LOST:
			GetCurrentState().onDisconnect(*this);
			break;
		case wxSOCKET_CONNECTION:
			GetCurrentState().onConnect(*this);
			break;
		default:
			break;
		}
	}
}

void wxCmdlineProtocol::SendLine(const wxString& msg)
{
	WX_SMTP_PRINT_DEBUG("Send to server : %s (%d)\n", msg.mb_str(wxConvLocal), msg.Length());

	wxString complete_line = msg;
	complete_line << _T("\x00d\x00a");
	wxSSLSocketClient::Write(complete_line.mb_str(wxConvLocal), complete_line.Length());
}

void wxCmdlineProtocol::OnTimerEvent(wxTimerEvent& WXUNUSED(event))
{
	GetCurrentState().onTimeout(*this);
}

void wxCmdlineProtocol::TimerStart(int seconds)
{
	timer.Start(seconds * 1000, wxTIMER_ONE_SHOT);
}

void wxCmdlineProtocol::TimerStop(void)
{
	timer.Stop();
}

void wxCmdlineProtocol::TimerRestart()
{
	timer.Start();
}

const wxCmdlineProtocol::State wxCmdlineProtocol::default_state = wxCmdlineProtocol::State();
