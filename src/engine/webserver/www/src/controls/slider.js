// A SLIDER — its value between its ends (MinValue, MaxValue). It shows; the thin client's sends nothing either.

import { Control, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Slider extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.slider = /** @type {any} */ (document.createElement('wa-slider'));
		this.slider.setAttribute('disabled', '');
		this.append(this.slider);
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		this.slider.min = node.MinValue ?? 0;
		this.slider.max = node.MaxValue ?? 100;
		this.slider.value = node.Value ?? 0;
		return true;
	}
}

defineControl('Slider', Slider);
