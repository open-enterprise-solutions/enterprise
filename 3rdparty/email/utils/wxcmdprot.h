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
 $Log: wxcmdprot.h,v $
 Revision 1.3  2003/11/13 17:12:15  tavasti
 - Muutettu tiedostojen nimet wx-alkuisiksi

 Revision 1.2  2003/11/07 09:17:40  tavasti
 - K‰‰ntyv‰ versio, fileheaderit lis‰tty.


****************************************************************************/

/*
 * Purpose: base class for all command line oriented internet protocols
 * Author:  Frank Buﬂ
 * Created: 2002
 */

#ifndef _WX_CMDPROT_H
#define _WX_CMDPROT_H

#include <wx/wx.h>

#include "../wxsmtpdef.h"

#include "../wxemail.h"

#include "../ssl/wxSSLSocketClient.h"

/**
 * \internal
 *
 * This class is the base class of all command-line oriented internet protocols.
 *
 * Each protocol shall derive a set of wxCmdlineProtocol::State. The system is always
 * in one (and only one) state. This class will propagate the different connection events
 * in the current state :
 *     - Connection events
 *     - Disconnection events
 *     - Response line from the server
 *     - Timeout events.
 *
 * This class offers mechanisms to
 *      - Change the current state
 *      - Send commands to the server
 *      - Set/reset timeout conditions that will be triggered
 *      - Initiate connection and disconnection to server
 *
 * Any command-line protocol can thus be implemented by implementing a
 * set of states, each one performing desired state transitions on the
 * different events generated, and implementing the communication with
 * the server.
 */
class WXDLLIMPEXP_SMTP wxCmdlineProtocol : public wxEvtHandler, public wxSSLSocketClient
{
   protected:

      /**
       * \internal
       *
       * This is the base class of a command-line internet protocol.
       *
       * The default implementation of each function performs nothing. The
       * user can thus perform desired actions by overriding requested
       * methods.
       *
       * Note that, as states are shared from one instance of the protocol to the other,
       * it is important that they do not contain any state, which is the reason why all
       * methods are const.
       */
      class WXDLLIMPEXP_SMTP State
      {
         public:

            /*!
             * This function is invoked each time the client succeeded in connection with
             * the server. This event will thus only occur after the user has explicitely
             * invoked the wxCmdlineProtocol::Connect method, once the client will be
             * effectively connected to the server.
             */
            virtual void onConnect(wxCmdlineProtocol& WXUNUSED(context)) const {}

            /*!
             * This function is invoked each time the connection to the server is closed.
             * This event can be generated at any time once the connection is established
             * as the connection can be interrupted by external conditions.
             */
            virtual void onDisconnect(wxCmdlineProtocol& WXUNUSED(context)) const {}

            /*!
             * This function is invoked each time the server sends a response (i.e. a line).
             * The line sent by the serve (not including the carriage return) is provided to
             * the function.
             */
            virtual void onResponse(wxCmdlineProtocol& WXUNUSED(context), const wxString& WXUNUSED(line)) const {}

            /*!
             * This function is invoked when a timeout condition occurs. The user is responsible
             * for handling those timeout conditions (which include starting condition timer AND
             * revoking them when expected event occurs). This event will thus be generated if the
             * timer was not revocated before timeout is reached.
             *
             * See functions wxCmdlineProtocol::TimerStart and wxCmdlineProtocol::TimerStop for
             * more detail.
             */
            virtual void onTimeout(wxCmdlineProtocol& WXUNUSED(context)) const {}

            /*!
             * This function is invoked when the state is entered.
             */
            virtual void onEnterState(wxCmdlineProtocol& WXUNUSED(context)) const {}

            /*!
             * This function is invoked when the state is left.
             */
            virtual void onLeaveState(wxCmdlineProtocol& WXUNUSED(context)) const {}

            /*!
             * Just to be sure we have a virtual destructor
             */
            virtual ~State() {}
      };

   public:

      /*!
       * Default constructor of the class
       */
      wxCmdlineProtocol();

      /*!
       * Default desstructor of the class
       */
      virtual ~wxCmdlineProtocol();

      /*!
       * This function starts a timer condition that will expire
       * (and thus, generate the wxCmdlineProtocol::State::onTimeout condition
       * on current state) if timer is not stopped within the next s seconds.
       *
       * @param s number of seconds before timer expiration
       */
      void TimerStart(int s);

      /*!
       * This function stops the current timer so that no
       * wxCmdlineProtocol::State::onTimeout condition will
       * be generated.
       */
      void TimerStop(void);

      /*!
       * This function stops the current timer and restarts it with previous
       * timeout value.
       */
      void TimerRestart();

      /*!
       * This function changes the current state of the system.
       *
       * @param state new state of the system
       */
      void ChangeState(const State& state)
      {
         current_state->onLeaveState(*this);
         current_state = &state;
         current_state->onEnterState(*this);
      }

      /*!
       * This function retrieves the current state of the system
       *
       * @return a reference to the current state of the system
       */
      const State& GetCurrentState() const {return *current_state;}

      /**
       * Sets the server and port information.
       *
       * No server connection will be established until wxCmdlineProtocol::Connect
       * is called.
       *
       * \param host server
       * \param port TCP port number
       */
      void SetHost(const wxString& host, const int port)
      {
         m_host = host;
         m_port = port;
      }

      /*!
       * This function performs the connection to the server.
       *
       * Note that this function does not wait for the connection to be established
       * before returning. Once the connection will be established, a
       * wxCmdlineProtocol::State::onConnect will be generated to the current state
       * of the system.
       */
      void Connect();

      /*!
       * This function performs the disconnection from the server and cleans-up all
       * internal stuff. Note that there is no need to invoke this function if
       * disconnection was performed by an external event.
       *
       * An event wxCmdlineProtocol::State::onDisconnect will be generated when the
       * connection will be effectively closed (which can happen without invoking this function
       * as well as after the invocation of this function).
       *
       * Note that if this function is invoked once the connection is already closed, no event
       * will be generated.
       */
      void Disconnect();

      /**
       * This function sends a new command line to the server.
       *
       * The carriage return is automatically added by this function. So,
       * it shall not be provided to the function.
       *
       * \param msg The string to be written.
       */
      void SendLine(const wxString& msg);

   private:

      /**
       * This function intercepts all socket events. It will dispatch
       * each event depending on its origin.
       */
      void OnSocketEvent(wxSocketEvent& event);

      /**
       * This method is called each time new data is available from socket.
       * It shall reconstitute the lines received from the server and invoke
       * the wxCmdlineProtocol::State::onResponse for each received line
       */
      void OnInput();

      /*!
       * This function is timer handling timeout conditions. It will dispatch
       * the event to the wxCmdlineProtocol::State::onTimeout
       */
      void OnTimerEvent(wxTimerEvent& event);

      /*!
       * The socket will be reused from one connection to another.
       * The following id is used to differentiate the different connections.
       */
      int m_socket_usage_id;
      void NextSocketSerialNr()
      {
         ++m_socket_usage_id;
      }
      int GetCurrentSocketSerialNr()
      {
         return m_socket_usage_id;
      }

      /*!
       * This is the instanciation of the default state of the system.
       * This state is entered after initialisation of the class and is never
       * entered anymore once the user has configured a new state.
       * This state performs nothing.
       */
      static const State default_state;

      /*!
       * this is the internal state of the class
       */
      wxString       m_host;
      int            m_port;
      wxString       m_inputLine;
      wxTimer        timer;
      State const*   current_state;

   DECLARE_EVENT_TABLE()
};

#endif /* _WX_CMDPROT_H */
