import type {JSX} from "solid-js";
import {Input} from "../ui/input";
import {Label} from "../ui/label";

export function Field(props: {
    id: string;
    label?: string;
    value: string;
    placeholder?: string;
    maxLength?: number;
    type?: "text" | "password" | "search";
    disabled?: boolean;
    onInput: (value: string) => void;
    onEnter?: () => void;
    onBlur?: () => void;
}): JSX.Element {
    return (
        <div>
            {props.label ? <Label for={props.id}>{props.label}</Label> : null}
            <Input
                id={props.id}
                type={props.type ?? "text"}
                value={props.value}
                maxLength={props.maxLength}
                placeholder={props.placeholder}
                disabled={props.disabled}
                spellcheck={false}
                autocomplete="off"
                onInput={(event) => props.onInput(event.currentTarget.value)}
                onBlur={() => props.onBlur?.()}
                onKeyDown={(event) => {
                    if (event.key === "Enter") {
                        props.onEnter?.();
                    }
                }}
            />
        </div>
    );
}
