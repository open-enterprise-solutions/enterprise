#pragma once

// Which selected control the designer's canvas may still draw its highlight around.
//
// The canvas (ibDesignerWindow) keeps RAW pointers to the selected control and to the wx objects made for
// it, and paints the highlight from them on every paint event. A control can go while the canvas still
// holds it — the form is rebuilt, a control is removed, an undo takes an added one away — and the next
// paint then reads a destroyed object: the designer died in HighlightSelection on a null vtable
// (EXC_BAD_ACCESS at 0x48) right after the configuration was saved.
//
// So a pointer is only ever followed after the OWNER of the wx tree has confirmed it still knows the
// control. The host's control map is keyed by the pointer and never dereferences it, which is what makes
// it a safe thing to ask about a pointer that may already be dead.
//
// Header only and free of the GUI, so the rule can be tested without a designer window.

#include <functional>

class ibValueFrame;

using ibSelectionLiveCheck = std::function<bool(const ibValueFrame*)>;

// May `selected` be dereferenced? Nothing selected is not live (there is nothing to draw); with no check
// installed the pointer is trusted, as it always was.
inline bool ibSelectionIsLive(const ibValueFrame* selected, const ibSelectionLiveCheck& isLive)
{
	if (selected == nullptr)
		return false;
	return !isLive || isLive(selected);
}
