// A CHECK BOX — its caption and whether it is checked; toggled back as event Toggle {Checked}.

import { Control, EventKind, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Checkbox extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.box = /** @type {any} */ (document.createElement('wa-checkbox'));
		this.box.setAttribute('size', 's');
		this.box.addEventListener('change', () => this.send(EventKind.Toggle, { Checked: this.box.checked === true }));
		this.lookTarget = this.box;
		this.append(this.box);
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		this.box.textContent = state.Caption ?? node.Title ?? '';
		this.box.checked = state.Checked === true;
		// A read-only box shows its mark and takes no click.
		this.box.toggleAttribute('disabled', state.ReadOnly === true);
		return true;
	}
}

defineControl('Checkbox', Checkbox);
