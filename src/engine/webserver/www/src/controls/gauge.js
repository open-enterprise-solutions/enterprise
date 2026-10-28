// A GAUGE — how far its value has come of its full mark (Range).

import { Control, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Gauge extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.bar = /** @type {any} */ (document.createElement('wa-progress-bar'));
		this.append(this.bar);
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const range = node.Range > 0 ? node.Range : 100;
		this.bar.value = Math.max(0, Math.min(100, ((node.Value ?? 0) / range) * 100));
		return true;
	}
}

defineControl('Gauge', Gauge);
