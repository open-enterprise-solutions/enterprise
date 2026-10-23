// A RADIO BUTTON — its caption and whether it is the one selected. It sends nothing: the thin client's does not
// either; the choice among them is the server's.

import { Control, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Radiobutton extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.mark = document.createElement('input');
		this.mark.type = 'radio';
		this.mark.disabled = true;
		this.text = document.createElement('span');
		this.append(this.mark, this.text);
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		this.text.textContent = state.Caption ?? '';
		this.mark.checked = state.Selected === true;
		return true;
	}
}

defineControl('Radiobutton', Radiobutton);
