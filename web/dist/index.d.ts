export * from './nprpc';
export * from './nprpc_nameserver';
export * from './utils';
import { Nameserver } from './nprpc_nameserver';
export declare function get_nameserver(nameserver_ip: string): Nameserver;
