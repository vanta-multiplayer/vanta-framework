import type {JSX} from "solid-js";

/**
 * Vanta's mark. Everything else comes from lucide-solid.
 */
export function IconMark(props: {class?: string}): JSX.Element {
    return (
        <svg class={props.class} viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg">
            <rect width="48" height="48" rx="12" fill="rgb(var(--primary-color))" />
            <path d="M14 34 24 12l10 22h-4.4L24 20.8 18.4 34H14Z" fill="#f6ead7" />
        </svg>
    );
}
