// A CAPTION (Statictext) — its title, or the value it is bound to (State's Text) when it is bound; a link when the
// State says so, pressed back as event Open.

import { Control, EventKind, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Statictext extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.addEventListener('click', () => {
			if (this.state(this.node).Link === true)
				this.send(EventKind.Open);
		});
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		this.textContent = state.Text ?? state.Caption ?? node.Title ?? '';
		this.classList.toggle('link', state.Link === true);
		return true;
	}
}

defineControl('Statictext', Statictext);
