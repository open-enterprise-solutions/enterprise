// AN HTML BOX — the page its State names, in a frame of its own: a sandbox with no scripts, so what a configuration
// writes there is shown and not run.

import { Control, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Htmlbox extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.frame = document.createElement('iframe');
		this.frame.setAttribute('sandbox', '');
		this.frame.title = node.Name ?? '';
		this.append(this.frame);
		this.shown = undefined;
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const page = this.state(node).Page ?? '';
		// Setting it reloads the frame: only a page that changed is set.
		if (page !== this.shown) {
			this.frame.srcdoc = page;
			this.shown = page;
		}
		return true;
	}
}

defineControl('Htmlbox', Htmlbox);
