// THE INPUT MASK — `+1 (000) 000-00-00;0;_` — what a field lets you type: the core's ibInputMask (core/inputMask.h,
// docs/private/input-mask.md) written over for the browser, the server's resolved mask parsed here as the desktop parses
// it. Only what an editor asks of it: a text laid into the mask (apply), a range replaced as a key, a deletion or a
// paste replaces it (replace), the run a double click selects (getRun). The value is read back out on the server
// (Strip): the editor sends the masked text.
//
// One position a character: the masked text is as long as the mask, a literal where the mask has one, the blank in a
// position nothing was typed into. A `!` first makes the mask a value's shape: digits with a point a NUMBER, typed as a
// number is — its text the number's own, no positions; a date pattern a DATE, whose digits make no impossible day,
// month, hour, minute or second.

const Kind = Object.freeze({ Literal: 0, Digit: 1, DigitOrSign: 2, Letter: 3, LetterOrDigit: 4, Any: 5 });
const Case = Object.freeze({ AsTyped: 0, Upper: 1, Lower: 2 });
const Part = Object.freeze({ None: 0, Day: 1, Month: 2, Year: 3, Hour: 4, Hour12: 5, Minute: 6, Second: 7 });

/** @typedef {{kind: number, literal: string, required: boolean, case: number, part: number}} Slot */
/** @typedef {{whole: number, fraction: number, signed: boolean}} NumberShape */

export class InputMask {
	constructor() {
		/** @type {Slot[]} */
		this.slots = [];
		/** @type {NumberShape | null} a number's mask — none: positions */
		this.number = null;
		this.blank = ' ';
	}

	/**
	 * The mask's text, `mask[;literals[;blank]]`; an empty text is no mask. Never fails: a character that is no position
	 * is a literal.
	 * @param {string} text
	 */
	static parse(text) {
		const mask = new InputMask();

		// THE MASK — up to the first `;` no `\` fences.
		let end = 0;
		for (; end < text.length && text[end] !== ';'; ++end) {
			if (text[end] === '\\')
				++end;
		}
		end = Math.min(end, text.length);
		const section = text.slice(0, end);

		// `!` first: the shape of a value. A number's is no positions at all.
		let datePattern = false;
		let from = 0;
		if (section[0] === '!') {
			from = 1;
			const shape = /^(#?)(9+)(?:\.(9+))?$/.exec(section.slice(1));
			if (shape !== null) {
				mask.number = { whole: shape[2].length, fraction: shape[3]?.length ?? 0, signed: shape[1] === '#' };
				from = end;
			}
			else
				datePattern = /[dMyHhms]/.test(section.slice(1));
		}
		// A date's time beside its date may be left out; a time alone may not.
		const hasDate = datePattern && /[yMd]/.test(section.slice(from));

		/** @type {number} */
		let slotCase = Case.AsTyped;
		const addLiteral = (/** @type {string} */ c) => {
			mask.slots.push({ kind: Kind.Literal, literal: c, required: false, case: Case.AsTyped, part: Part.None });
		};
		const addPosition = (/** @type {number} */ kind, /** @type {boolean} */ required, /** @type {number} */ part = Part.None) => {
			mask.slots.push({ kind, literal: '', required, case: slotCase, part });
		};

		for (let i = from; i < section.length; ++i) {
			const c = section[i];
			if (c === '\\') {
				if (i + 1 < section.length)
					addLiteral(section[++i]);
				continue;
			}
			if (datePattern) {
				switch (c) {
				case 'd': addPosition(Kind.Digit, true, Part.Day); break;
				case 'M': addPosition(Kind.Digit, true, Part.Month); break;
				case 'y': addPosition(Kind.Digit, true, Part.Year); break;
				case 'H': addPosition(Kind.Digit, !hasDate, Part.Hour); break;
				case 'h': addPosition(Kind.Digit, !hasDate, Part.Hour12); break;
				case 'm': addPosition(Kind.Digit, !hasDate, Part.Minute); break;
				case 's': addPosition(Kind.Digit, !hasDate, Part.Second); break;
				default: addLiteral(c); break;
				}
				continue;
			}
			switch (c) {
			case '!': break;   // not first: Delphi's, which is not ours
			case '>': slotCase = Case.Upper; break;
			case '<':
				if (section[i + 1] === '>') {
					++i;
					slotCase = Case.AsTyped;
				}
				else
					slotCase = Case.Lower;
				break;
			case '0': addPosition(Kind.Digit, true); break;
			case '9': addPosition(Kind.Digit, false); break;
			case '#': addPosition(Kind.DigitOrSign, false); break;
			case 'L': addPosition(Kind.Letter, true); break;
			case 'l': addPosition(Kind.Letter, false); break;
			case 'A': addPosition(Kind.LetterOrDigit, true); break;
			case 'a': addPosition(Kind.LetterOrDigit, false); break;
			case 'C': addPosition(Kind.Any, true); break;
			case 'c': addPosition(Kind.Any, false); break;
			default: addLiteral(c); break;
			}
		}

		// THE LITERALS section is the server's (Strip). THE BLANK — the character after the second `;`; a space when there
		// is none.
		const second = text.indexOf(';', end + 1);
		if (end < text.length && second >= 0 && second + 1 < text.length)
			mask.blank = text[second + 1];
		return mask;
	}

	isEmpty() {
		return this.slots.length === 0 && this.number === null;
	}

	/**
	 * A text laid into the mask: its characters that fit, in order, literals matched or stepped over — what an editor
	 * starts from. The empty text gives the skeleton; a text already masked comes back as it is. A number's is the
	 * number's own text.
	 * @param {string} text
	 */
	apply(text) {
		if (this.number !== null)
			return applyNumber(this.number, text);
		if (this.isEmpty() || this.fits(text))
			return text;
		const masked = this.slots.map((slot) => slot.kind === Kind.Literal ? slot.literal : this.blank);
		const caret = { at: 0 };
		for (let i = 0; i < text.length; ++i) {   // by UTF-16 units, as the core's ibString is read
			if (text[i] !== this.blank)
				this.type(masked, caret, text[i]);   // a character no position takes is dropped
		}
		return masked.join('');
	}

	/**
	 * ⭐ THE ONE EDIT — the range [from, to) of a masked text replaced by `text`, as ibTextCtrl's Replace: a key typed
	 * (an empty range at the caret), a deletion (no text), a paste. Positions are blanked, not removed; literals stay.
	 * Null when the mask takes nothing of it — the key is refused.
	 * @param {string} masked
	 * @param {number} from
	 * @param {number} to
	 * @param {string} text
	 * @param {number} caret where the caret stood — a deletion of literals alone goes the way it leaves
	 * @returns {{masked: string, caret: number} | null}
	 */
	replace(masked, from, to, text, caret) {
		if (this.isEmpty())
			return null;

		if (this.number !== null) {
			const shape = this.number;
			const shaped = applyNumber(shape, masked);
			let edited = shaped;
			from = Math.min(from, edited.length);
			to = Math.min(Math.max(to, from), edited.length);
			const caretBefore = Math.min(caret, edited.length);
			// The range goes as text does — unless what is left is no number of the shape.
			if (from < to) {
				edited = edited.slice(0, from) + edited.slice(to);
				if (applyNumber(shape, edited) !== edited)
					return null;
			}
			const at = { text: edited, caret: from };
			for (let i = 0; i < text.length; ++i) {
				if (text[i] !== this.blank)
					typeNumber(shape, at, text[i]);   // a character the number does not take is dropped
			}
			return at.text === shaped && at.caret === caretBefore ? null : { masked: at.text, caret: at.caret };
		}

		const count = this.slots.length;
		const before = this.fits(masked) ? masked : this.apply(masked);
		const chars = before.split('');
		from = Math.min(from, count);
		to = Math.min(Math.max(to, from), count);
		const caretBefore = Math.min(caret, count);

		if (from < to) {
			// A deletion of literals alone reaches the nearest position on the side the caret leaves.
			let hasPosition = false;
			for (let i = from; i < to && !hasPosition; ++i)
				hasPosition = !this.isLiteral(i);
			if (!hasPosition && text === '') {
				if (caretBefore >= to) {
					let p = from;
					while (p > 0 && this.isLiteral(p - 1))
						--p;
					if (p === 0)
						return null;
					from = p - 1;
				}
				else {
					let p = to;
					while (p < count && this.isLiteral(p))
						++p;
					if (p === count)
						return null;
					from = p;
				}
				to = from + 1;
			}
			for (let i = from; i < to; ++i) {
				if (!this.isLiteral(i))
					chars[i] = this.blank;
			}
		}

		// What is typed goes on from where the range began.
		const at = { at: from };
		for (let i = 0; i < text.length; ++i) {
			if (text[i] !== this.blank)
				this.type(chars, at, text[i]);
		}

		// A literal typed changes no character, only where the caret stands — and that is the key taken too.
		const after = chars.join('');
		return after !== before || at.at !== caretBefore ? { masked: after, caret: at.at } : null;
	}

	/**
	 * THE RUN AT `position` — the positions between two literals the caret there stands in or just after: a day, a
	 * month; a number's whole part or its fraction. What a double click selects, [first, last] both included; null
	 * where there is none.
	 * @param {string} masked
	 * @param {number} position
	 * @returns {{first: number, last: number} | null}
	 */
	getRun(masked, position) {
		if (this.number !== null) {
			const shaped = applyNumber(this.number, masked);
			const sign = shaped[0] === '-' ? 1 : 0;
			const point = shaped.indexOf('.');
			const [first, end] = point < 0 || position <= point ? [sign, point < 0 ? shaped.length : point]
				: [point + 1, shaped.length];
			return first < end ? { first, last: end - 1 } : null;
		}
		const count = this.slots.length;
		let slot = position;
		if (slot >= count || this.isLiteral(slot)) {
			if (slot === 0 || slot > count || this.isLiteral(slot - 1))
				return null;
			--slot;
		}
		return this.runOf(slot);
	}

	/** @param {number} slot */
	isLiteral(slot) {
		return this.slots[slot].kind === Kind.Literal;
	}

	/**
	 * @param {number} slot
	 * @param {string} c
	 */
	accepts(slot, c) {
		if (c === this.blank)
			return false;   // a blank typed would be a position left empty
		switch (this.slots[slot].kind) {
		case Kind.Digit:         return c >= '0' && c <= '9';
		case Kind.DigitOrSign:   return (c >= '0' && c <= '9') || c === '-' || c === '+';
		case Kind.Letter:        return /^\p{L}$/u.test(c);
		case Kind.LetterOrDigit: return /^[\p{L}\p{Nd}]$/u.test(c);
		case Kind.Any:           return c >= ' ';
		default:                 return false;
		}
	}

	/**
	 * @param {number} slot
	 * @param {string} c
	 */
	cased(slot, c) {
		const changed = this.slots[slot].case === Case.Upper ? c.toUpperCase()
			: this.slots[slot].case === Case.Lower ? c.toLowerCase() : c;
		return changed.length === 1 ? changed : c;   // one place, one character: `ß` stays itself
	}

	/** The run of positions `slot` stands in — between two literals. */
	runOf(/** @type {number} */ slot) {
		let first = slot;
		let last = slot;
		while (first > 0 && !this.isLiteral(first - 1))
			--first;
		while (last + 1 < this.slots.length && !this.isLiteral(last + 1))
			++last;
		return { first, last };
	}

	/** Is the text already masked — as long as the mask, its literals, each position blank or a character it takes. */
	fits(/** @type {string} */ text) {
		if (text.length !== this.slots.length)
			return false;
		return this.slots.every((slot, i) => slot.kind === Kind.Literal ? text[i] === slot.literal
			: text[i] === this.blank || this.accepts(i, text[i]));
	}

	/**
	 * One character typed at the caret; false when no position takes it.
	 * @param {string[]} masked
	 * @param {{at: number}} caret
	 * @param {string} c
	 */
	type(masked, caret, c) {
		const count = this.slots.length;
		let p = Math.min(caret.at, count);

		// Literals before the next position: the one typed is stepped past, any other stepped over.
		while (p < count && this.isLiteral(p)) {
			if (stepsPast(this.slots[p].literal, c)) {
				caret.at = p + 1;
				return true;
			}
			++p;
		}
		if (p === count)
			return false;

		if (this.accepts(p, c)) {
			const was = masked[p];
			masked[p] = this.cased(p, c);
			if (this.takesPart(masked, p)) {
				caret.at = p + 1;
				return true;
			}
			masked[p] = was;
		}

		// Not taken here: a literal further on that IS this character is stepped past — `.` typed in a day goes on to the
		// month.
		for (let q = p + 1; q < count; ++q) {
			if (this.isLiteral(q) && stepsPast(this.slots[q].literal, c)) {
				caret.at = q + 1;
				return true;
			}
		}
		return false;
	}

	/**
	 * The date part `slot` belongs to, the digit just put there: false when no value of the part begins so; the digits
	 * after it that make it impossible are blanked — `15` typed over with `3` is `3_`.
	 * @param {string[]} masked
	 * @param {number} slot
	 */
	takesPart(masked, slot) {
		const part = this.slots[slot].part;
		if (part === Part.None)
			return true;
		const { first, last } = this.runOf(slot);
		let least = 0;
		let most = 0;
		switch (part) {
		case Part.Day: least = 1; most = 31; break;
		case Part.Month: least = 1; most = 12; break;
		case Part.Hour: least = 0; most = 23; break;
		case Part.Hour12: least = 1; most = 12; break;
		case Part.Minute: case Part.Second: least = 0; most = 59; break;
		default: most = 10 ** (last - first + 1) - 1; least = last - first + 1 >= 4 ? 1 : 0; break;   // a year
		}

		// The digits from the part's start to the slot — a beginning no value of the part has is refused.
		let prefix = 0;
		let gap = false;
		for (let i = first; i <= slot && !gap; ++i) {
			gap = masked[i] === this.blank;
			if (!gap)
				prefix = prefix * 10 + Number(masked[i]);
		}
		if (!gap) {
			const scale = 10 ** (last - slot);
			if (prefix * scale > most || prefix * scale + scale - 1 < least)
				return false;
		}

		// What stands after it, where it makes the part impossible, goes: `15` typed over with `3` is `3_`, then `30`.
		let value = 0;
		let whole = true;
		for (let i = first; i <= last && whole; ++i) {
			whole = masked[i] !== this.blank;
			if (whole)
				value = value * 10 + Number(masked[i]);
		}
		if (whole && (value < least || value > most)) {
			for (let i = slot + 1; i <= last; ++i)
				masked[i] = this.blank;
		}
		return true;
	}
}

/**
 * Does `c` typed step past the literal — itself, and a decimal point either way: `,` typed goes past a `.`.
 * @param {string} literal
 * @param {string} c
 */
function stepsPast(literal, c) {
	return c === literal || (isPoint(literal) && isPoint(c));
}

/** @param {string} c */
function isPoint(c) {
	return c === '.' || c === ',';
}

/** @param {string} c */
function isDigit(c) {
	return c >= '0' && c <= '9';
}

/**
 * A text made the number's: a sign, the digits, the point — what does not fit dropped (`1 234,500` is `1234.500`).
 * @param {NumberShape} shape
 * @param {string} text
 */
function applyNumber(shape, text) {
	let negative = false;
	let point = false;
	let digits = false;
	let whole = '';
	let fraction = '';
	for (const c of text) {
		if (isDigit(c)) {
			digits = true;
			if (!point && whole.length < shape.whole)
				whole += c;
			else if (point && fraction.length < shape.fraction)
				fraction += c;
		}
		else if (c === '-' && shape.signed && !digits && !point)
			negative = true;
		else if (isPoint(c) && shape.fraction > 0 && !point)
			point = true;
		// anything else — a group's space, a blank, a letter — is no part of a number
	}
	return (negative ? '-' : '') + whole + (point ? '.' + fraction : '');
}

/**
 * One character typed into a number at the caret: a digit where it stands, the point, the sign; false when not taken.
 * @param {NumberShape} shape
 * @param {{text: string, caret: number}} at
 * @param {string} c
 */
function typeNumber(shape, at, c) {
	const text = at.text;
	const sign = text[0] === '-' ? 1 : 0;
	const point = text.indexOf('.');
	let caret = Math.min(at.caret, text.length);

	// The sign: `-` puts it before the number or takes it away, `+` takes it away.
	if (c === '-' || c === '+') {
		if (!shape.signed)
			return false;
		if (sign !== 0) {
			at.text = text.slice(1);
			at.caret = Math.max(caret - 1, 0);
			return true;
		}
		if (c === '+')
			return false;
		at.text = '-' + text;
		at.caret = caret + 1;
		return true;
	}

	caret = Math.max(caret, sign);   // nothing goes before the sign

	// The point: where it already is, the caret goes past it; where it is not, it goes in at the caret when the
	// fraction can hold what stands after it.
	if (isPoint(c)) {
		if (shape.fraction === 0)
			return false;
		if (point >= 0) {
			at.caret = point + 1;
			return true;
		}
		if (text.length - caret > shape.fraction)
			return false;
		at.text = text.slice(0, caret) + '.' + text.slice(caret);
		at.caret = caret + 1;
		return true;
	}

	if (!isDigit(c))
		return false;

	if (point < 0 || caret <= point) {
		const wholeEnd = point < 0 ? text.length : point;
		// A lone zero gives way to the first digit typed after it: `0.000` and `5` is `5.000`.
		if (wholeEnd - sign === 1 && text[sign] === '0' && caret === sign + 1) {
			at.text = text.slice(0, sign) + c + text.slice(sign + 1);
			at.caret = caret;
			return true;
		}
		if (wholeEnd - sign >= shape.whole)
			return false;   // the whole part is full
		at.text = text.slice(0, caret) + c + text.slice(caret);
		at.caret = caret + 1;
		return true;
	}

	// The fraction: a digit goes in while there is room, and over the next one when there is none.
	if (text.length - point - 1 < shape.fraction) {
		at.text = text.slice(0, caret) + c + text.slice(caret);
		at.caret = caret + 1;
		return true;
	}
	if (caret < text.length) {
		at.text = text.slice(0, caret) + c + text.slice(caret + 1);
		at.caret = caret + 1;
		return true;
	}
	return false;
}
