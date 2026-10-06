#ifndef __CLIENT_REQUEST_H__
#define __CLIENT_REQUEST_H__

// WHAT THE SERVER ASKS A CLIENT — the kinds of request the frame's Request carries (ibClientFrame::Request), all of
// them here, as the methods a client asks the server are in clientMethod.h. A number on the wire, a type here. Each
// kind is a small protocol of its own; the numbers are the protocol's: never renumbered, a new kind takes the next one.
enum class ibRequestKind : int {
	Message = 1,   // a message box: Text, Caption, Style (the wx button flags) → Button (the wx code, 0 for none)
	Choice  = 2,   // one of a list: Caption, items (Id, Caption, Picture, Selected) → Id
	Help    = 3,   // a text to read: Title, Text → nothing
};

#endif
