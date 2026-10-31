// A LINE (Staticline) — across, or down when its orientation says so.

import { Control, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// A line's orientation (wxVERTICAL).
const vertical = 8;

class Staticline extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		this.setAttribute('orient', node.Orient === vertical ? 'vertical' : 'horizontal');
		return true;
	}
}

defineControl('Staticline', Staticline);
