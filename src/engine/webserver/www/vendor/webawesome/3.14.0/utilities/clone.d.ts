/**
 * Recursively clones plain objects and arrays, passing everything else (functions, class instances, DOM nodes, dates,
 * maps, sets, etc.) through by reference. Unlike `structuredClone`, this won't throw on non-serializable values, which
 * makes it useful for defensively copying config objects that may contain callbacks (e.g. Chart.js configs).
 *
 * Only the spine of plain objects and arrays is duplicated. Non-plain values are shared, not copied, so mutating one
 * through the clone also mutates the original. Circular references are handled. For JSON-safe data, use
 * `structuredClone` instead.
 */
export declare function deepClone<T>(value: T, seen?: WeakMap<object, unknown>): T;
export default deepClone;
/**
 * Returns `true` if the value is a plain object — i.e. an object literal whose prototype is `Object.prototype`. Arrays,
 * `null`, class instances, `Date`, `Map`, DOM nodes, etc. all return `false`. Useful for deciding whether a value can be
 * safely recursed into and merged key-by-key.
 */
export declare function isPlainObject(value: unknown): value is Record<string, unknown>;
/**
 * Recursively merges `target` into a clone of `src`, returning a new object. Where both sides hold a plain object at the
 * same key, the two are merged recursively; otherwise `target`'s value overrides `src`'s. Arrays and non-plain values
 * (functions, class instances, etc.) are not merged element-by-element — `target` replaces `src` wholesale at that key.
 *
 * `src` is never mutated (it is cloned via {@link deepClone}, so functions and other non-serializable values it
 * contains are preserved by reference). `target`'s values are assigned by reference, so do not mutate the result's
 * nested objects if you need `target` to stay pristine.
 */
export declare function deepMerge<T extends Record<string, unknown>, U extends Record<string, unknown>>(src: T, target: U): T & U;
