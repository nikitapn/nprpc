import { Exception } from "./exception";

export interface ref<T> {
	value: T;
}

export function make_ref<T = any>(): ref<T> {
	return { value: undefined };
}

export const sip4_to_u32 = (str: string): number => {
	let rx = /(\d+)\.(\d+)\.(\d+)\.(\d+)/ig;
	let parts = rx.exec(str);
	if (parts.length != 5) throw "ip address is incorrect";
	return Number(parts[1]) << 24 | Number(parts[2]) << 16 | Number(parts[3]) << 8 | Number(parts[4]);
}

export const ip4tostr = (ip4: number): string => {
  return (ip4 >> 24 & 0xFF) + '.' + (ip4 >> 16 & 0xFF) + '.' + (ip4 >> 8 & 0xFF) + '.' + (ip4 & 0xFF);
}

export class MyPromise<T = void, E extends Exception = Exception>  {
	private actual_promise_: Promise<T>
	private resolve_: (x: T) => void;
	private reject_: (e: E) => void;

	constructor() {
		this.actual_promise_ = new Promise<T>((resolve, reject) => {
			this.resolve_ = (x) => { resolve(x) };
			this.reject_ = (x) => { reject(x) };
		});
	}

	public get $() { return this.actual_promise_; }

	public set_promise(value: T): void {
		this.resolve_(value);
	}

	public set_exception(ec: E): void {
		this.reject_(ec);
	}
}