export { Exception } from './base';
export * from './nprpc';
export * from './utils';
export * as Flat from './flat';
export { FlatBuffer } from './flat_buffer';
export * from './gen/nprpc_base';
export * from './gen/nprpc_nameserver';
import { Nameserver } from './gen/nprpc_nameserver';
export declare function get_nameserver(nameserver_ip: string): Nameserver;
