// THE ELEMENT UNDER THE FOCUS DOES IT — the thin client's EditFocused: Cut, Copy, Paste, Delete and Select All done by
// what the person was in, on what it holds selected. Asked from the Edit menu, which takes the focus as it opens — so it
// is the element last focused outside a menu — and by the server (the question Edit: a form's command asked back).
//
// The element is told first — an event `edit` {command, text} at it, as the thin client sends the menu's command to the
// window in focus: one that edits its own way (a sheet: its cells copied, a text pasted from its current cell, every
// cell taken) answers it and says so (preventDefault). Otherwise a field does the browser's own command on its
// selection; anything else does nothing, as a window that answers none of them.

import { Command } from '../docView/tabDocument.js';

// What edits text of its own — and keeps its own Undo and Redo: a field typed in.
export const textFields = 'input:not([type=radio]):not([type=checkbox]), textarea, [contenteditable]';

// The browser's command for each edit (Paste is read from the clipboard: a page may not paste by a command).
/** @type {Record<number, string>} */
const commands = { [Command.Cut]: 'cut', [Command.Copy]: 'copy', [Command.Delete]: 'delete',
	[Command.SelectAll]: 'selectAll' };

/** @type {HTMLElement | null} the element last focused outside a menu — the innermost: the field inside a component */
let focused = null;

addEventListener('focusin', (event) => {
	const path = event.composedPath();
	if (path.some((node) => node instanceof Element && node.matches('nav.menu, wa-dropdown')))
		return;
	focused = path[0] instanceof HTMLElement ? path[0] : null;
});

/**
 * An edit done by the element last focused, given the focus back first. A paste takes the clipboard where the browser
 * lets it be read.
 * @param {number} command (Command: Cut … SelectAll)
 */
export async function editFocused(command) {
	const element = focused?.isConnected ? focused : null;
	if (element === null)
		return;
	element.focus();
	const text = command === Command.Paste ? await navigator.clipboard?.readText().catch(() => '') ?? '' : '';
	const edit = new CustomEvent('edit', { detail: { command, text }, bubbles: true, composed: true, cancelable: true });
	if (!element.dispatchEvent(edit) || !element.matches(textFields))
		return;
	// The clipboard read, the focus may have gone back to the menu that asked.
	element.focus();
	if (command === Command.Paste) {
		if (text !== '')
			document.execCommand('insertText', false, text);
	}
	else if (commands[command] !== undefined)
		document.execCommand(commands[command]);
}
