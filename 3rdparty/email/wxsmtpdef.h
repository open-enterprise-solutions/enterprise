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

****************************************************************************/

/*!
 *
 * \if coders
 * \mainpage wxSMTP Project (Developpers version)
 * \else
 * \mainpage wxSMTP Project
 * \endif
 *
 * \section intro_sec Introduction
 *
 * wxSMTP is a wxWidgets contrib that allows sending and receiving e-mails.
 *
 * \if coders
 *
 * This documentation is intended for persons willing to develop/support the wxSMTP
 * project. If you only manage to use this library, you should envisage reading
 * the user-documentation
 *
 * \endif
 *
 * It currently supports two protocols :
 * \li \c SMTP This protocol is used for sending e-mails
 * \li \c POP3 This protocol allows retrieving mails from (almost) all mailboxes
 *
 * \section smtp SMTP
 *
 * In order to use the SMTP protocol, two main classes shall be used :
 * \li \c ::wxSmtpEmailMessage represents an e-mail message ready to be sent via SMTP protocol
 * \li \c ::wxSMTP is the class handling the communication with an SMTP server
 *
 * \section pop3 POP-3
 *
 * In order to use the POP3 protocol, two main classes shall be used :
 * \li \c ::wxPOP3EmailMessage represents an e-mail message as received from the main server
 * \li \c ::wxPOP3 is the class handling the communication with a POP3 server
 */


#ifndef _WX_SMTP_DEFS_H_
#define _WX_SMTP_DEFS_H_

// Defines for component version.
// The following symbols should be updated for each new component release
// since some kind of tests, like those of AM_WXCODE_CHECKFOR_COMPONENT_VERSION()
// for "configure" scripts under unix, use them.
#define wxSMTP_MAJOR          1
#define wxSMTP_MINOR          13
#define wxSMTP_RELEASE        0

// For non-Unix systems (i.e. when building without a configure script),
// users of this component can use the following macro to check if the
// current version is at least major.minor.release
#define wxCHECK_SMTP_VERSION(major,minor,release) \
    (wxSMTP_MAJOR > (major) || \
    (wxSMTP_MAJOR == (major) && wxSMTP_MINOR > (minor)) || \
    (wxSMTP_MAJOR == (major) && wxSMTP_MINOR == (minor) && wxSMTP_RELEASE >= (release)))


// Defines for shared builds.
// Simple reference for using these macros and for writin components
// which support shared builds:
//
// 1) use the WXDLLIMPEXP_SMTP in each class declaration:
//          class WXDLLIMPEXP_SMTP myCompClass {   [...]   };
//
// 2) use the WXDLLIMPEXP_SMTP in the declaration of each global function:
//          WXDLLIMPEXP_MYCOMP int myGlobalFunc();
//
// 3) use the WXDLLIMPEXP_DATA_SMTP() in the declaration of each global
//    variable:
//          WXDLLIMPEXP_DATA_SMTP(int) myGlobalIntVar;
//
//#ifdef WXMAKINGDLL_WXSMTP
//    #define WXDLLIMPEXP_SMTP                    WXEXPORT
//    #define WXDLLIMPEXP_DATA_SMTP(type)         WXEXPORT type
//#elif defined(WXUSINGDLL)
//    #define WXDLLIMPEXP_SMTP                    WXIMPORT
//    #define WXDLLIMPEXP_DATA_SMTP(type)         WXIMPORT type
//#else // not making nor using DLL
    #define WXDLLIMPEXP_SMTP
    #define WXDLLIMPEXP_DATA_SMTP(type)         type
//#endif

#ifdef WX_SMTP_DEBUG
#define WX_SMTP_PRINT_DEBUG(...) wxLogDebug(wxString::Format(__VA_ARGS__));
#else
#define WX_SMTP_PRINT_DEBUG(...)
#endif

#endif // _WX_SMTP_DEFS_H_
