// A TYPE DESCRIPTION EDITED — the types a value may hold and their qualifiers, as the thin client's type picker
// (ibShowTypeSelector, frmclient/win/dlgs/typeSelector.cpp): the types offered, each ticked or not — one, or several once
// Composite data type is ticked; under them the qualifiers of the type last pressed: a string's length (0 — no limit) and
// whether a shorter one is padded to it, a number's length, scale and sign, a date's fraction. The offer is the server's
// (Classes: Type — the class id, Name, Picture), in its order.
//
// THE REFERENCES UNDER THEIR FAMILY, as the thin's tree sorts them, by the id itself: a type of no metaobject — a
// primitive, a family (`CatalogRef`) — stands at the root, and a family is a type and a heading at once, its members
// below it; a reference whose family is not offered goes under a plain heading of the family's name where the server
// named it, at the root otherwise.
//
// The description travels in its stored form (ibTypeDescriptionMemory): Types — [{TypeId}], the class id as text (a
// 64-bit id is no JSON number); Precision, Scale, NonNegative; DateFraction (0 date, 1 date and time, 2 time); Length
// (0 — no limit), AllowedLength (0 variable, 1 fixed).

import { _ } from '../localization.js';
import { Type } from '../settings/fieldTree.js';

// The most a string's length and a number's digits may say (MAX_LENGTH_STRING, MAX_PRECISION_NUMBER — core/core.h), and
// the length a string takes back when its no-limit check goes off (ibQualifierString's).
const maxLength = 1024;
const maxPrecision = 32;
const defaultLength = 10;

// A class id read as the core reads it (core/clsid.h): its kind the high byte, a metaobject's value — a reference, an
// object… — of the kinds 0x10–0x1D, its metaID the low 32 bits, ANY (0) in a family's.
const metaIdMask = 0xFFFFFFFFn;
/** @param {string} id */
const kindOf = (id) => Number(BigInt(id) >> 56n);
/** @param {string} id */
const isMetaValue = (id) => kindOf(id) >= 0x10 && kindOf(id) <= 0x1D;
/** @param {string} id */
const isAny = (id) => isMetaValue(id) && (BigInt(id) & metaIdMask) === 0n;
/** @param {string} id */
const familyOf = (id) => String(BigInt(id) & ~metaIdMask);
/** @param {string} id */
const isReference = (id) => kindOf(id) === 0x10;

/**
 * @param {Record<string, any>} stored the description now
 * @param {Record<string, any>[]} classes the types offered — Type, Name, Picture
 * @param {(picture: any) => string} pictureUrl
 * @returns {{element: HTMLElement, edited: () => Record<string, any>}}
 */
export function typeEditor(stored, classes, pictureUrl) {
	const held = new Set((stored.Types ?? []).map((/** @type {any} */ one) => String(one.TypeId ?? '')));
	const named = new Map(classes.map((one) => [String(one.Type), one]));

	const element = document.createElement('div');
	element.className = 'type-selector';

	const composite = labelled(checkBox(), _('Composite data type'));
	composite.input.checked = held.size > 1;

	const list = document.createElement('div');
	list.className = 'choice-list';

	/** @type {{id: string, input: HTMLInputElement}[]} */
	const ticks = [];
	/** @type {Map<string, HTMLElement>} a family's place: its members go in it */
	const headings = new Map();

	/**
	 * A type's line — its tick, its picture and its name.
	 * @param {string} id
	 * @param {Record<string, any>} one
	 */
	const typeLine = (id, one) => {
		const input = checkBox();
		input.checked = held.has(id);
		input.addEventListener('change', () => {
			// One type, unless several are allowed: the one ticked now is the one held.
			if (input.checked && !composite.input.checked)
				ticks.forEach((other) => { other.input.checked = other.input === input; });
		});
		ticks.push({ id, input });
		const label = document.createElement('label');
		label.append(input, picture(pictureUrl(one.Picture)), one.Name ?? '');
		label.addEventListener('click', () => show(id));
		return label;
	};

	/**
	 * A line and the place under it its members go in.
	 * @param {HTMLElement} at
	 * @param {HTMLElement} line
	 */
	const group = (at, line) => {
		const members = document.createElement('div');
		members.className = 'type-members';
		at.append(line, members);
		return members;
	};

	for (const one of classes) {
		const id = String(one.Type);
		if (!isMetaValue(id) || isAny(id)) {
			headings.set(id, group(list, typeLine(id, one)));
			continue;
		}
		const family = familyOf(id);
		const familyNamed = named.get(family);
		if (!headings.has(family) && isReference(id) && familyNamed !== undefined) {
			const heading = document.createElement('div');
			heading.className = 'type-heading';
			heading.append(picture(pictureUrl(one.Picture)), familyNamed.Name ?? '');
			headings.set(family, group(list, heading));
		}
		(headings.get(family) ?? list).append(typeLine(id, one));
	}

	composite.input.addEventListener('change', () => {
		// Several no longer allowed: the first ticked is the one kept, as the thin takes it.
		if (composite.input.checked)
			return;
		const first = ticks.find((one) => one.input.checked);
		ticks.forEach((one) => { one.input.checked = one === first; });
	});

	const length = numberInput(0, maxLength, stored.Length ?? defaultLength);
	const allowedLength = choice([_('Variable'), _('Fixed')], stored.AllowedLength ?? 0);
	const unlimited = labelled(checkBox(), _('Unlimited'));
	let lastLength = Number(stored.Length ?? 0) > 0 ? Number(stored.Length) : defaultLength;
	/** @param {boolean} noLimit */
	const holdUnlimited = (noLimit) => {
		unlimited.input.checked = noLimit;
		length.disabled = noLimit;
		allowedLength.disabled = noLimit;
	};
	holdUnlimited(Number(stored.Length ?? 0) === 0);
	length.addEventListener('change', () => {
		const value = whole(length, 0, maxLength);
		if (value > 0)
			lastLength = value;
		else
			allowedLength.selectedIndex = 0;
		holdUnlimited(value === 0);
	});
	unlimited.input.addEventListener('change', () => {
		const noLimit = unlimited.input.checked;
		length.value = String(noLimit ? 0 : lastLength);
		if (noLimit)
			allowedLength.selectedIndex = 0;
		holdUnlimited(noLimit);
	});

	const precision = numberInput(0, maxPrecision, stored.Precision ?? 10);
	const scale = numberInput(0, maxPrecision, stored.Scale ?? 0);
	const nonNegative = labelled(checkBox(), _('Non-negative'));
	nonNegative.input.checked = stored.NonNegative === true;
	// The scale never passes the length — the description's own rule (ibQualifierNumber::ScaleWithin): a length made
	// shorter takes the scale down with it, a scale beyond the length stops at it.
	const keepScale = () => {
		const digits = whole(precision, 0, maxPrecision);
		if (digits > 0 && whole(scale, 0, maxPrecision) > digits)
			scale.value = String(digits);
	};
	precision.addEventListener('change', keepScale);
	scale.addEventListener('change', keepScale);

	const fraction = choice([_('Date'), _('Date and time'), _('Time')], stored.DateFraction ?? 0);

	const stringLine = line(caption(_('Length:')), length, allowedLength, unlimited.label);
	const numberLine = line(caption(_('Length:')), precision, caption(_('Scale:')), scale, nonNegative.label);
	const dateLine = line(caption(_('Date format:')), fraction);

	// Only the qualifiers of the type last pressed are shown — the thin's rule: a reference has none of them.
	/** @param {string | undefined} id */
	const show = (id) => {
		stringLine.hidden = id !== Type.String;
		numberLine.hidden = id !== Type.Number;
		dateLine.hidden = id !== Type.Date;
	};
	show(ticks.find((one) => one.input.checked)?.id);

	element.append(composite.label, list, stringLine, numberLine, dateLine);
	return {
		element,
		edited: () => {
			const noLimit = unlimited.input.checked || whole(length, 0, maxLength) === 0;
			return {
				Types: ticks.filter((one) => one.input.checked).map((one) => ({ TypeId: one.id })),
				Precision: whole(precision, 0, maxPrecision),
				Scale: whole(scale, 0, maxPrecision),
				NonNegative: nonNegative.input.checked,
				DateFraction: fraction.selectedIndex,
				Length: noLimit ? 0 : whole(length, 0, maxLength),
				AllowedLength: noLimit ? 0 : allowedLength.selectedIndex,
			};
		},
	};
}

/**
 * A number field's value, whole and within its bounds.
 * @param {HTMLInputElement} input
 * @param {number} min
 * @param {number} max
 */
function whole(input, min, max) {
	return Math.max(min, Math.min(max, Math.trunc(Number(input.value) || 0)));
}

/** @param {string} url */
function picture(url) {
	const image = document.createElement('img');
	image.src = url;
	image.alt = '';
	image.hidden = url === '';
	return image;
}

/** @param {...(Node | string)} content */
function line(...content) {
	const element = document.createElement('div');
	element.className = 'type-selector-line';
	element.append(...content);
	return element;
}

/** @param {string} text */
function caption(text) {
	const element = document.createElement('span');
	element.textContent = text;
	return element;
}

/**
 * @param {HTMLInputElement} input
 * @param {string} text
 */
function labelled(input, text) {
	const label = document.createElement('label');
	label.append(input, text);
	return { label, input };
}

function checkBox() {
	const input = document.createElement('input');
	input.type = 'checkbox';
	return input;
}

/**
 * @param {number} min
 * @param {number} max
 * @param {number} value
 */
function numberInput(min, max, value) {
	const input = document.createElement('input');
	input.type = 'number';
	input.min = String(min);
	input.max = String(max);
	input.value = String(value);
	return input;
}

/**
 * @param {string[]} captions
 * @param {number} selected
 */
function choice(captions, selected) {
	const select = document.createElement('select');
	for (const text of captions)
		select.append(new Option(text));
	select.selectedIndex = selected;
	return select;
}
