
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