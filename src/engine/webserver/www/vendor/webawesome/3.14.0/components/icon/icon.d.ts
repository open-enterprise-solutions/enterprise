import WebAwesomeElement from '../../internal/webawesome-element.js';
import type { HTMLTemplateResult, PropertyValues } from 'lit';
export type IconAnimation = 'beat' | 'fade' | 'beat-fade' | 'bounce' | 'flip' | 'flip-360' | 'shake' | 'spin' | 'spin-pulse' | 'spin-reverse' | 'spin-snap' | 'spin-snap-4' | 'spin-snap-8' | 'buzz' | 'wag' | 'float' | 'swing' | 'jello';
export type IconCanvas = 'fixed' | 'auto' | 'square' | 'roomy';
/**
 * @summary Icons are scalable vector symbols that represent actions, content, or status throughout your application.
 *  They support Font Awesome and custom icon libraries with animation presets.
 * @documentation https://webawesome.com/docs/components/icon
 * @status stable
 * @since 2.0
 *
 * @event wa-load - Emitted when the icon has loaded. When using `spriteSheet: true` this will not emit.
 * @event wa-error - Emitted when the icon fails to load due to an error. When using `spriteSheet: true` this will not emit.
 *
 * @csspart svg - The internal SVG element.
 * @csspart use - The `<use>` element generated when using `spriteSheet: true`
 *
 * @cssproperty [--animation-delay=0] Sets when the animation will start.
 * @cssproperty [--animation-direction=normal] Defines whether or not the animation should play in reverse on alternate cycles.
 * @cssproperty [--animation-duration=1s] Defines the length of time that an animation takes to complete one cycle.
 * @cssproperty [--animation-iteration-count=infinite] Defines the number of times an animation cycle is played.
 * @cssproperty [--animation-timing] Describes how the animation will progress over one cycle of its duration.
 * @cssproperty [--beat-fade-opacity] Set lowest opacity value an icon with `beat-fade` animation will fade to and from.
 * @cssproperty [--beat-fade-scale] Set max value that an icon with `beat-fade` animation will scale.
 * @cssproperty [--beat-scale] Set the scale multiplier for an icon with `beat` animation. This multiplies the animation's 1.25× base pulse, so the default `1.25` peaks at ~1.56× and `2` roughly doubles the pulse.
 * @cssproperty [--bounce-height] Set the max height an icon with `bounce` animation will jump to when bouncing.
 * @cssproperty [--bounce-jump-scale-x] Set the icon’s horizontal distortion (“squish”) at the top of the jump.
 * @cssproperty [--bounce-jump-scale-y] Set the icon’s vertical distortion (“squish”) at the top of the jump.
 * @cssproperty [--bounce-land-scale-x] Set the icon’s horizontal distortion (“squish”) when landing after the jump.
 * @cssproperty [--bounce-land-scale-y] Set the icon’s vertical distortion (“squish”) when landing after the jump.
 * @cssproperty [--bounce-rebound] Set the amount of rebound an icon with `bounce` animation has when landing after the jump.
 * @cssproperty [--bounce-start-scale-x] Set the icon’s horizontal distortion (“squish”) when starting to bounce.
 * @cssproperty [--bounce-start-scale-y] Set the icon’s vertical distortion (“squish”) when starting to bounce.
 * @cssproperty [--fade-opacity] Set lowest opacity value an icon with `fade` animation will fade to and from.
 * @cssproperty [--flip-angle] Set rotation angle of flip for an icon with `flip` or `flip-360` animation. A positive angle denotes a clockwise rotation, a negative angle a counter-clockwise one.
 * @cssproperty [--flip-x] Set x-coordinate of the vector denoting the axis of rotation (between 0 and 1) for an icon with `flip` or `flip-360` animation.
 * @cssproperty [--flip-y] Set y-coordinate of the vector denoting the axis of rotation (between 0 and 1) for an icon with `flip` or `flip-360` animation.
 * @cssproperty [--flip-z] Set z-coordinate of the vector denoting the axis of rotation (between 0 and 1) for an icon with `flip` or `flip-360` animation.
 * @cssproperty [--flip-anticipation-scale] Set the scale of the wind-up before an icon with `flip` or `flip-360` animation rotates.
 * @cssproperty [--flip-overshoot] Set how far past the final angle an icon with `flip` or `flip-360` animation rotates before settling.
 * @cssproperty [--bounce-anticipation] Set the downward squash distance before an icon with `bounce` animation jumps.
 * @cssproperty [--buzz-distance] Set the horizontal travel of an icon with `buzz` animation.
 * @cssproperty [--wag-angle] Set the peak rotation of an icon with `wag` animation.
 * @cssproperty [--swing-angle] Set the peak rotation of an icon with `swing` animation.
 * @cssproperty [--jello-scale-x] Set the horizontal stretch of an icon with `jello` animation.
 * @cssproperty [--jello-scale-y] Set the vertical stretch of an icon with `jello` animation.
 * @cssproperty [--float-height] Set the rise height of an icon with `float` animation.
 * @cssproperty [--float-drift] Set the horizontal drift of an icon with `float` animation.
 * @cssproperty [--float-tilt] Set the rotation of an icon with `float` animation.
 * @cssproperty [--float-squash-x] Set the horizontal squash of an icon with `float` animation at rest.
 * @cssproperty [--float-squash-y] Set the vertical squash of an icon with `float` animation at rest.
 * @cssproperty [--float-stretch-x] Set the horizontal stretch of an icon with `float` animation at its peak.
 * @cssproperty [--float-stretch-y] Set the vertical stretch of an icon with `float` animation at its peak.
 * @cssproperty [--primary-color=currentColor] - Sets a duotone icon's primary color.
 * @cssproperty [--primary-opacity=1] - Sets a duotone icon's primary opacity.
 * @cssproperty [--secondary-color=currentColor] - Sets a duotone icon's secondary color.
 * @cssproperty [--secondary-opacity=0.4] - Sets a duotone icon's secondary opacity.
 */
export default class WaIcon extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private svg;
    /** The name of the icon to draw. Available names depend on the icon library being used. */
    name?: string;
    /**
     * The family of icons to choose from. For Font Awesome Free, valid options include `classic` and `brands`. For
     * Font Awesome Pro subscribers, valid options include, `classic`, `sharp`, `duotone`, `sharp-duotone`, and `brands`.
     * A valid kit code must be present to show pro icons via CDN. You can set `<html data-fa-kit-code="...">` to provide
     * one.
     */
    family: string;
    /**
     * The name of the icon's variant. For Font Awesome, valid options include `thin`, `light`, `regular`, and `solid` for
     * the `classic` and `sharp` families. Some variants require a Font Awesome Pro subscription. Custom icon libraries
     * may or may not use this property.
     */
    variant: string;
    /**
     * Sets the icon canvas — the box the icon is centered within. Unset renders as `fixed` (1.25em × 1em); `auto` hugs the
     * icon's width; `square` is 1.25em × 1.25em; `roomy` is 1.5em × 1.5em. Mirrors Font Awesome's `fa-fixed-width`,
     * `fa-width-auto`, `fa-canvas-square`, and `fa-canvas-roomy`. Scales with `font-size`.
     */
    canvas?: IconCanvas;
    /**
     * Sets the width of the icon to match the cropped SVG viewBox. This operates like the Font `fa-width-auto` class.
     *
     * @deprecated Use `canvas="auto"` instead.
     */
    autoWidth: boolean;
    /** Swaps the opacity of duotone icons. */
    swapOpacity: boolean;
    /**
     * An external URL of an SVG file. Be sure you trust the content you are including, as it will be executed as code and
     * can result in XSS attacks.
     */
    src?: string;
    /**
     * An alternate description to use for assistive devices. If omitted, the icon will be considered presentational and
     * ignored by assistive devices.
     */
    label: string;
    /** The name of a registered custom icon library. */
    library: string;
    /** Sets the rotation degree of the icon */
    rotate: number;
    /** Sets the flip direction of the icon along the 'x' (horizontal), 'y' (vertical), or 'both' axes. */
    flip?: 'x' | 'y' | 'both';
    /** Sets the animation for the icon */
    animation?: IconAnimation;
    connectedCallback(): void;
    firstUpdated(changedProperties: PropertyValues<this>): void;
    disconnectedCallback(): void;
    private getIconSource;
    /** Given a URL, this function returns the resulting SVG element or an appropriate error symbol. */
    private resolveIcon;
    handleLabelChange(): void;
    setIcon(): Promise<void>;
    willUpdate(changedProperties: PropertyValues<this>): void;
    updated(changedProperties: PropertyValues<this>): void;
    render(): SVGElement | HTMLTemplateResult | null;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-icon': WaIcon;
    }
}
