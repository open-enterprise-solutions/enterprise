// A RADIO BUTTON — its caption and whether it is the one selected; clicked back as event Toggle. Which one of a switch
// is selected is the server's (its source's value, or its own Selected), so no two share a name here; a read-only one
// puts the click back itself.

import { Control, EventKind, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Radiobutton extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.mark = document.createElement('input');
		this.mark.type = 'radio';
		this.mark.addEventListener('change', () => {
			if (this.readOnly) {
				this.mark.checked = false;
				return;
			}
			this.send(EventKind.Toggle);
		});
		this.text = document.createElement('span');
		this.append(this.mark, this.text);
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		this.text.textContent = state.Caption ?? '';
		this.mark.checked = state.Selected === true;
		this.readOnly = state.ReadOnly === true;
		return true;
	}
}

defineControl('Radiobutton', Radiobutton);
