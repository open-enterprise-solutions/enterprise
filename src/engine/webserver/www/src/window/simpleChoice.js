// A NUMBER'S CALCULATOR AND A DATE'S CALENDAR — the thin client's simple choice (frmclient/visualView/ctrl/
// typeControl.cpp, ibTypeControlFactory::SimpleChoice), dropped under the field pressed. The server asks it from the
// field's Select (question 12, SimpleChoice) and takes the value chosen; the page only draws it.

/**
 * A date's calendar — the day and the time, and OK.
 * @param {string} value the field's date as the wire writes it, "yyyy-mm-dd hh:mm:ss"; "" — the empty date
 * @param {(chosen: string) => void} take called with the date chosen, written the same way
 * @returns {HTMLElement}
 */
export function calendar(value, take) {
	const form = document.createElement('form');
	form.className = 'calendar';
	const input = document.createElement('input');
	input.type = 'datetime-local';
	input.step = '1';
	input.autofocus = true;
	// The empty date opens on today, as the desktop's calendar does.
	const today = new Date();
	const day = `${today.getFullYear()}-${pad(today.getMonth() + 1)}-${pad(today.getDate())}T00:00:00`;
	input.value = /^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d$/.test(value) ? value.replace(' ', 'T') : day;
	const ok = document.createElement('button');
	ok.type = 'submit';
	ok.textContent = 'OK';
	form.append(input, ok);
	form.addEventListener('submit', (event) => {
		event.preventDefault();
		// Seconds left out when they are naught ("2026-10-23T14:05").
		const [date = '', time = ''] = input.value.split('T');
		take(date === '' ? '' : `${date} ${`${time}:00:00`.slice(0, 8)}`);
	});
	return form;
}

// The operations a key opens, and the sign each is shown by above the figure it waits for.
const signs = { '+': '+', '-': '−', '*': '×', '/': '÷' };

/**
 * A number's pocket calculator: the field's value on the display, a figure keyed in or worked out, OK puts the result
 * into the field. It counts in exact decimals, as the thin client's ibNumber does — 0.1 + 0.2 is 0.3 — left to right
 * like any pocket calculator ("12 + 3 *" finishes 12 + 3 first), a result cut at ten places. Keys typed work as the
 * ones pressed; Enter finishes an open operation first, and only then takes the result.
 * @param {string} value the field's number, as text
 * @param {(chosen: string) => void} take called with the result, as text
 * @returns {HTMLElement}
 */
export function calculator(value, take) {
	const box = document.createElement('div');
	box.className = 'calculator';
	box.tabIndex = -1;
	box.autofocus = true;
	const operation = document.createElement('div');
	operation.className = 'operation';
	const display = document.createElement('output');
	display.className = 'display';

	/** the figure being typed, exactly as typed ("0.", "12.50") */
	let entry = '';
	/** the left side of the open operation, or the last result */
	let accumulator = decimal(value);
	/** @type {keyof typeof signs | ''} */
	let pending = '';
	/** what the display stands for: the last result, the figure being typed, or a division by zero */
	let state = 'result';

	/** @param {Decimal} initial */
	const reset = (initial) => {
		entry = '';
		accumulator = initial;
		pending = '';
		state = 'result';
	};
	const beginEntry = () => {
		if (state === 'error')
			reset(decimal('0'));
		if (state !== 'typing') {
			entry = '';
			state = 'typing';
		}
	};
	/** @param {number} digit */
	const digitKey = (digit) => {
		beginEntry();
		// A leading zero is not kept ("007" is 7), and typing has a length: the field it ends up in is a number of a
		// declared size.
		if (entry === '0')
			entry = '';
		else if (entry === '-0')
			entry = '-';
		if (entry.replace(/\D/g, '').length < 20)
			entry += String(digit);
	};
	const point = () => {
		beginEntry();
		if (entry.includes('.'))
			return;
		if (entry === '' || entry === '-')
			entry += '0';
		entry += '.';
	};
	const backspace = () => {
		if (state === 'error')
			reset(decimal('0'));
		if (state !== 'typing')
			return;   // a result is not typed text; there is nothing to take a character off
		entry = entry.slice(0, -1);
		if (entry === '-')
			entry = '';
	};
	const negate = () => {
		if (state === 'typing')
			entry = entry.startsWith('-') ? entry.slice(1) : `-${entry}`;
		else if (state === 'result')
			accumulator = { units: -accumulator.units, scale: accumulator.scale };
	};
	// The typed figure is taken: the right side of the open operation, or the value itself.
	const takeEntry = () => {
		const right = decimal(entry.endsWith('.') ? entry.slice(0, -1) : entry);
		entry = '';
		state = 'result';
		if (pending === '')
			accumulator = right;
		else if (pending === '/') {
			if (right.units === 0n) {
				state = 'error';
				return;
			}
			accumulator = divide(accumulator, right, 10);
		}
		else
			accumulator = round(pending === '*' ? multiply(accumulator, right)
				: add(accumulator, pending === '-' ? { units: -right.units, scale: right.scale } : right), 10);
	};
	// Two operators in a row ("12 + *") replace one another, they do not combine.
	/** @param {keyof typeof signs} op */
	const operator = (op) => {
		if (state === 'typing')
			takeEntry();
		if (state !== 'error')
			pending = op;
	};
	const equals = () => {
		if (state === 'typing')
			takeEntry();
		pending = '';
	};
	const ok = () => {
		equals();
		if (state === 'error')
			return;   // 12 / 0 is not a value to put into the field
		take(text(accumulator));
	};
	const show = () => {
		display.value = state === 'error' ? 'Error' : state === 'typing' ? (entry || '0') : text(accumulator);
		operation.textContent = pending === '' ? '\u00a0' : signs[pending];
	};

	const keys = document.createElement('div');
	keys.className = 'keys';
	/**
	 * @param {string} label
	 * @param {() => void} action
	 */
	const key = (label, action) => {
		const button = document.createElement('button');
		button.type = 'button';
		button.textContent = label;
		button.addEventListener('click', () => {
			action();
			show();
			box.focus();
		});
		keys.append(button);
		return button;
	};
	key('C', () => reset(decimal('0')));
	key('⌫', backspace);
	key('±', negate);
	key(signs['/'], () => operator('/'));
	for (const [row, op] of /** @type {[number, keyof typeof signs][]} */ ([[2, '*'], [1, '-'], [0, '+']])) {
		for (let col = 0; col < 3; col++) {
			const digit = row * 3 + col + 1;
			key(String(digit), () => digitKey(digit));
		}
		key(signs[op], () => operator(op));
	}
	key('0', () => digitKey(0));
	key('.', point);
	key('=', equals);
	key('OK', ok).className = 'ok';

	box.addEventListener('keydown', (event) => {
		const pressed = event.key;
		if (pressed === 'Enter') {
			if (pending !== '')
				equals();
			else {
				ok();
				event.preventDefault();
				return;
			}
		}
		else if (pressed === 'Backspace')
			backspace();
		else if (pressed === 'Delete')
			reset(decimal('0'));
		else if (/^\d$/.test(pressed))
			digitKey(Number(pressed));
		else if (pressed === '.' || pressed === ',')
			point();
		else if (pressed in signs)
			operator(/** @type {keyof typeof signs} */ (pressed));
		else if (pressed === '=')
			equals();
		else
			return;
		event.preventDefault();
		show();
	});

	box.append(operation, display, keys);
	show();
	return box;
}

/** @param {number} part */
function pad(part) {
	return String(part).padStart(2, '0');
}

// AN EXACT DECIMAL — units over a power of ten, so a sum of tenths is a sum of tenths and not of the binary fractions
// nearest them. Enough of ibNumber for a calculator: add, multiply, divide cut at some places, and its shortest text.

/** @typedef {{units: bigint, scale: number}} Decimal the number units / 10^scale */

/**
 * A number read from its text; anything else is naught.
 * @param {string} spelled
 * @returns {Decimal}
 */
function decimal(spelled) {
	const parts = /^(-?)(\d*)(?:\.(\d*))?$/.exec(spelled.trim());
	if (parts === null)
		return { units: 0n, scale: 0 };
	const fraction = parts[3] ?? '';
	const units = BigInt(`${parts[2] || '0'}${fraction}`);
	return { units: parts[1] === '-' ? -units : units, scale: fraction.length };
}

/**
 * @param {Decimal} a
 * @param {number} scale not less than a's
 */
function unitsAt(a, scale) {
	return a.units * 10n ** BigInt(scale - a.scale);
}

/**
 * @param {Decimal} a
 * @param {Decimal} b
 * @returns {Decimal}
 */
function add(a, b) {
	const scale = Math.max(a.scale, b.scale);
	return { units: unitsAt(a, scale) + unitsAt(b, scale), scale };
}

/**
 * @param {Decimal} a
 * @param {Decimal} b
 * @returns {Decimal}
 */
function multiply(a, b) {
	return { units: a.units * b.units, scale: a.scale + b.scale };
}

/**
 * A quotient has no end in general (1 / 3): it is cut at the places given, the last one rounded half away from naught.
 * @param {Decimal} a
 * @param {Decimal} b not naught
 * @param {number} places
 * @returns {Decimal}
 */
function divide(a, b, places) {
	const tenfold = (a.units * 10n ** BigInt(b.scale + places + 1)) / (b.units * 10n ** BigInt(a.scale));
	return { units: halfAway(tenfold), scale: places };
}

/**
 * @param {Decimal} a
 * @param {number} places
 * @returns {Decimal}
 */
function round(a, places) {
	if (a.scale <= places)
		return a;
	return { units: halfAway(a.units / 10n ** BigInt(a.scale - places - 1)), scale: places };
}

/** A number of tenths, rounded half away from naught to whole units. @param {bigint} tenths */
function halfAway(tenths) {
	return (tenths + (tenths < 0n ? -5n : 5n)) / 10n;
}

/**
 * The shortest exact spelling: no trailing zeros after the point, no bare point, no "-0".
 * @param {Decimal} a
 */
function text(a) {
	const negative = a.units < 0n;
	const digits = (negative ? -a.units : a.units).toString().padStart(a.scale + 1, '0');
	const whole = digits.slice(0, digits.length - a.scale);
	const fraction = digits.slice(digits.length - a.scale).replace(/0+$/, '');
	const spelled = fraction === '' ? whole : `${whole}.${fraction}`;
	return negative && spelled !== '0' ? `-${spelled}` : spelled;
}
