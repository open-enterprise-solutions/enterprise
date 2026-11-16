// THE ASSISTANT WINDOW — a tab the server opened (Kind Assistant, Administration → Assistant window, there while the
// person's assistant access runs), as the thin client's (frmclient/docView/docViewAssistant.cpp): the exchange with
// the assistant — what was said, what was answered, what it did — and what the person writes to it, sent as event
// Change {Text} on the tab; Enter sends, Shift+Enter is a second line. Under the transcript, while a message waits:
// collected by nobody yet, or being processed (State Waiting, Pending).

import { EventKind } from '../controls/control.js';
import { _ } from '../localization.js';
import { TabDocument, defineDocument } from './tabDocument.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../window/window.js').ClientWindow} ClientWindow */

// The window's kind of tab (ibView::ibKind).
const assistant = 13;

export class AssistantDocument extends TabDocument {
	/**
	 * @param {ClientWindow} window
	 * @param {number} tab
	 */
	constructor(window, tab) {
		super(window, tab);
		this.transcript = document.createElement('div');
		this.waiting = document.createElement('div');
		this.input = document.createElement('textarea');
		/** the entries of the transcript drawn — it only grows */
		this.drawn = 0;
	}

	/** @param {ProtocolNode} view */
	drawView(view) {
		if (this.element === null) {
			this.element = document.createElement('div');
			this.element.className = 'assistant-window';
			this.transcript.className = 'assistant-transcript';
			const hint = document.createElement('div');
			hint.textContent = _('What you write goes to the connected assistant. The platform calls no model itself.');
			this.waiting.className = 'assistant-waiting';
			const send = document.createElement('wa-button');
			send.setAttribute('size', 's');
			send.textContent = _('Send');
			send.title = _('Leaves your message for the connected assistant to collect.');
			send.addEventListener('click', () => this.sendText());
			this.input.rows = 4;
			// Enter sends — the button's verb, reached as a person types; Shift holds it back for a second line.
			this.input.addEventListener('keydown', (event) => {
				if (event.key === 'Enter' && !event.shiftKey && !event.ctrlKey && !event.altKey) {
					event.preventDefault();
					this.sendText();
				}
			});
			const row = document.createElement('div');
			row.className = 'assistant-input';
			row.append(this.input, send);
			this.element.append(this.transcript, hint, this.waiting, row);
			this.window.views.append(this.element);
		}

		const rows = view.Rows?.NodeChildren ?? [];
		const grown = this.drawn < rows.length;
		for (; this.drawn < rows.length; this.drawn++) {
			const entry = document.createElement('div');
			entry.className = 'assistant-entry';
			entry.textContent = rows[this.drawn].Text ?? '';
			this.transcript.append(entry);
		}
		if (grown)
			this.transcript.scrollTop = this.transcript.scrollHeight;

		// Two silences, told apart by the server: a message nobody collected yet, and one being worked on.
		const state = view.State ?? {};
		this.waiting.textContent = state.Waiting !== true ? ''
			: (state.Pending ?? 0) > 0 ? _('Waiting for an assistant to collect it') : _('Processing the request');
	}

	/** What the person wrote, handed over and cleared. */
	sendText() {
		const text = this.input.value;
		if (text === '')
			return;
		this.input.value = '';
		this.input.focus();
		this.window.session.send({ Event: EventKind.Change, Args: { Text: text }, Tab: this.tab })
			.catch((error) => this.window.refused(error));
	}
}

defineDocument(assistant, AssistantDocument);
