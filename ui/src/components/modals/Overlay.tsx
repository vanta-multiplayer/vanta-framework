import type {JSX} from "solid-js";
import {Dialog} from "../ui/dialog";

export function Overlay(props: {children: JSX.Element; class?: string; onClose?: () => void}): JSX.Element {
    return (
        <Dialog class={props.class} onClose={props.onClose}>
            {props.children}
        </Dialog>
    );
}
