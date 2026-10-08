#include "widgets.h"
#include "form.h"
#include "core/serialize/dataBuilder.h"   // ibDataNode — an event's arguments
#include "backend/compiler/procUnit.h"

//*******************************************************************
//*                             Events                              *
//*******************************************************************

bool ibValueStaticText::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	if (event != ibProtocolEvent::Open)
		return ibValueWindow::OnClientEvent(event, args);
	OnHyperlinkClicked();
	return true;
}

void ibValueStaticText::OnHyperlinkClicked()
{
	// The SAME shape the text box's open button has: raise Opening, and let the configuration turn
	// the standard processing off if it means to open something of its own.
	ibValue standartProcessing = true;
	ibValueControl::CallAsEvent(m_eventOpening, GetValue(), standartProcessing);
	if (!standartProcessing.GetBoolean())
		return;

	// OPEN THE VALUE — and that is the whole handler. WHICH window opens is the value's own
	// answer: a reference opens its card, a schedule opens its editor, a type with no window does
	// nothing. No switch here, because one would grow a case per type while the values already
	// know.
	ibValue value;
	if (!GetControlValue(value) || value.IsEmpty())
		return;

	// OPEN IT — and nothing else. Whether anything CHANGED is not this control's question to
	// answer: opening a reference shows an object that may be edited to pieces while the reference
	// itself stays the reference, and opening a schedule edits the value in place. Only the window
	// that did the editing knows which happened, so it is the one that reports it (the schedule
	// editor marks the form on OK — mainFrameParts.cpp). The text shown IS the value's
	// presentation, so the next Update re-reads it and nothing more is needed here.
	value.ShowValue();
}
