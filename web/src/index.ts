// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

export * from './nprpc'
export * from './nprpc_nameserver'

import {Nameserver, _INameserver_Servant} from './nprpc_nameserver'


function sip4_to_u32(str: string): number {
	let rx = /(\d+)\.(\d+)\.(\d+)\.(\d+)/ig;
	let parts = rx.exec(str);
	if (parts.length != 5) throw "ip address is incorrect";
	return Number(parts[1]) << 24 | Number(parts[2]) << 16 | Number(parts[3]) << 8 | Number(parts[4]);
}

export function get_nameserver(nameserver_ip: string): Nameserver {
	let obj = new Nameserver();
	obj.data.ip4 = sip4_to_u32(nameserver_ip);
	obj.data.port = 15000;
	obj.data.websocket_port = 15001;
	obj.data.poa_idx = 0;
	obj.data.object_id = 0n;
	obj.data.flags = 0;
	obj.data.class_id = _INameserver_Servant._get_class();
	return obj;
}