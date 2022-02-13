# nprpc

CORBA-like RPC.\
The IDL language is almost similar to CORBA IDL.\
The proxy-stub code can be generated either for C++ or Typescript.\
It's a part of my DCS system.

# Examples
Server example: https://github.com/nikitapn/nprpc/blob/main/npnameserver/npnameserver.cpp \
Client example:
```
export async function rpc_init() {
	let rpc = NPRPC.init();
	poa = rpc.create_poa(32);

	let nameserver = NPRPC.get_nameserver("192.168.1.2");

	{
		let obj = NPRPC.make_ref<NPRPC.ObjectProxy>();
		await nameserver.Resolve("npsystem_webserver", obj);

		webserver = NPRPC.narrow(obj.value, WS.WebServer);
		if (!webserver) throw "WS.WebServer narrowing failed";
	}
	{
		let obj = NPRPC.make_ref<NPRPC.ObjectProxy>();
		await nameserver.Resolve("npsystem_server", obj);

		server = NPRPC.narrow(obj.value, SRV.Server);
		if (!server) throw "SRV.Server narrowing failed";
	}

	await webserver.get_web_categories(cats);

	//console.log(cats);

	let item_manager: SRV.ItemManager = null;
	{
		let obj = NPRPC.make_ref<NPRPC.ObjectProxy>();
		await server.CreateItemManager(obj);
		item_manager = NPRPC.narrow(obj.value, SRV.ItemManager);
		if (!item_manager) throw "SRV.ItemManager narrowing failed";
		item_manager.add_ref();
	}

	let data_callback = new OnDataCallbackImpl();
	let oid = poa.activate_object(data_callback);
	await item_manager.Activate(oid);

	let valid_items = new Array<{desc: WS.WebItem, ov: online_value}>();
	for (let cat of cats) {
		for (let item of cat.items) {
			let ov = new online_value(item.type);
			(item as any).ov = ov;
			if (item.dev_addr != 0xFF) valid_items.push({desc: item, ov: ov});
		}
	}

	let ar = new Array<SRV.DataDef>(valid_items.length);
	
	for (let i = 0; i < valid_items.length; ++i) {
		ar[i] = {
			dev_addr: valid_items[i].desc.dev_addr, 
			mem_addr: valid_items[i].desc.mem_addr, 
			type: valid_items[i].desc.type
		};
	}

	let handles = new Array<bigint>();
	await item_manager.Advise(ar, handles);

	for (let i = 0; i < valid_items.length; ++i) {
		data_callback.items_.set(handles[i], valid_items[i]);
	}
}
```
