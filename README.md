# nprpc

CORBA-like RPC.\
The IDL language is similar to CORBA.\
The proxy-stub code could be generated either for C++ or Typescript.\
It's a part of my DCS system.

# Examples

Client
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
Server
```
int main(int argc, char* argv[]) {
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	setvbuf(stdout, nullptr, _IOFBF, 1000);
#endif
	g_cfg.load("npwebserver");
	std::cout << g_cfg << std::endl;
  
	boost::asio::signal_set signals(thread_pool::get_instance().ctx(), SIGINT, SIGTERM);
	signals.async_wait(
		[&](boost::beast::error_code const&, int) {
			thread_pool::get_instance().stop();
		});

	IWebServerImpl web_server;

	try {
		nprpc::Config rpc_cfg;
		rpc_cfg.debug_level = nprpc::DebugLevel::DebugLevel_Critical;
		rpc_cfg.port = g_cfg.socket_port;
		rpc_cfg.websocket_port = g_cfg.websocket_port;
		rpc_cfg.http_root_dir = g_cfg.doc_root;

		auto rpc = nprpc::init(thread_pool::get_instance().ctx(), std::move(rpc_cfg));
		auto p1 = std::make_unique<nprpc::Policy_Lifespan>(nprpc::Policy_Lifespan::Persistent);
		auto poa = rpc->create_poa(2, { p1.get() });
		
		auto oid = poa->activate_object(&web_server);
		
		auto nameserver = rpc->get_nameserver(g_cfg.nameserver_ip);
		odb::Database::init(nameserver.get(), poa, g_cfg.data_dir / "keys", "npwebserver");

		nameserver->Bind(oid, "npsystem_webserver");

		rpc->start();
		thread_pool::get_instance().ctx().run();
	} catch (std::exception& ex) {
		std::cerr << ex.what();
		return EXIT_FAILURE;
	}

	std::cout << "server is shutting down..." << std::endl;

	return EXIT_SUCCESS;
}
```
