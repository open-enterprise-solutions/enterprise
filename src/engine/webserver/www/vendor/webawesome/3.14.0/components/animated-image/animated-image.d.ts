import { type PropertyValues } from 'lit';
import WebAwesomeElement from '../../internal/webawesome-element.js';
import '../icon/icon.js';
/**
 * @summary Animated images display GIFs and WEBPs with controls to play and pause them on demand. Use them when you
 *  want motion but need to give users control over when it plays.
 * @documentation https://webawesome.com/docs/components/animated-image
 * @status stable
 * @since 2.0
 *
 * @dependency wa-icon
 *
 * @event wa-load - Emitted when the image loads successfully.
 * @event wa-error - Emitted when the image fails to load.
 *
 * @slot play-icon - Optional play icon to use instead of the default. Works best with `<wa-icon>`.
 * @slot pause-icon - Optional pause icon to use instead of the default. Works best with `<wa-icon>`.
 *
 * @csspart control-box - The container that surrounds the pause/play icons and provides their background.
 *
 * @cssproperty --control-box-size - The size of the icon box.
 * @cssproperty --icon-size - The size of the play/pause icons.
 *
 * @ssr - Due to browser limitations, `<wa-animated-image>` can't render during SSR. As a fallback you can use a `<video>` tag, but its controls won't work, and the gif or webp will always auto-play.
 */
export default class WaAnimatedImage extends WebAwesomeElement {
    static css: import("lit").CSSResult;
    private readonly localize;
    animatedImage: HTMLImageElement;
    frozenFrame: string;
    isLoaded: boolean;
    /** The path to the image to load. */
    src: string;
    /** A description of the image used by assistive devices. */
    alt: string;
    /** Plays the animation. When this attribute is remove, the animation will pause. */
    play: boolean;
    private handleClick;
    private handleKeyDown;
    firstUpdated(changedProperties: PropertyValues<this>): void;
    private handleLoad;
    private handleError;
    handlePlayChange(): void;
    handleSrcChange(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-animated-image': WaAnimatedImage;
    }
}
