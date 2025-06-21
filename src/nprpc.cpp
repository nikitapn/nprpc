// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/uuid.hpp>

#include <boost/uuid/uuid_io.hpp>

#include <fstream>
#include <functional>

#ifdef _WIN32
#include <boost/asio/ssl/context.hpp>
#include <wincrypt.h>
namespace {
void add_windows_root_certs(boost::asio::ssl::context &ctx)
{
    HCERTSTORE hStore = CertOpenSystemStore(0, "ROOT");
    if (hStore == NULL) {
        return;
    }

    X509_STORE *store = X509_STORE_new();
    PCCERT_CONTEXT pContext = NULL;
    while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != NULL) {
        X509 *x509 = d2i_X509(NULL,
                              (const unsigned char **)&pContext->pbCertEncoded,
                              pContext->cbCertEncoded);
        if(x509 != NULL) {
            X509_STORE_add_cert(store, x509);
            X509_free(x509);
        }
    }

    CertFreeCertificateContext(pContext);
    CertCloseStore(hStore, 0);

    SSL_CTX_set_cert_store(ctx.native_handle(), store);
}
} // namespace
#endif // BOOST_OS_WINDOWS

using namespace nprpc;

namespace nprpc {

NPRPC_API RpcBuilder::RpcBuilder() {
  auto& uuid = impl::SharedUUID::instance().get();
  memcpy(cfg_.uuid.data(), &uuid, 16);

  if (1) {
    std::string buf(36, '0');
    bool ret = boost::uuids::to_chars(
      reinterpret_cast<const boost::uuids::uuid&>(uuid),
      buf.data(), buf.data() + buf.size()
    );
    assert(ret);
    std::cout << "nprpc UUID: " << buf << std::endl;
  }
}

NPRPC_API Rpc* RpcBuilder::build(boost::asio::io_context& ioc)
{
  using namespace impl;

  if (impl::g_orb) 
    throw Exception("rpc has been previously initialized");

  auto& ctx_server = cfg_.ssl_context_server;
  auto& ctx_client = cfg_.ssl_context_client;

  if (use_ssl_websocket_server_) {
    auto read_file_to_string = [](std::string const file) {
      std::ifstream is(file, std::ios_base::in);
      if (!is) { throw std::runtime_error("could not open certificate file: \"" + file + "\""); }
      return std::string(std::istreambuf_iterator<char>(is),
                         std::istreambuf_iterator<char>());
    };

    std::string const cert = read_file_to_string(ssl_public_key_path_);
    std::string const key = read_file_to_string(ssl_secret_key_path_);

    // ctx.set_password_callback(
    //     [](std::size_t, ssl::context_base::password_purpose) {
    //       return "test";
    //     });

    ctx_server.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::no_tlsv1 |      // Also disable TLS 1.0
        boost::asio::ssl::context::no_tlsv1_1 |    // Also disable TLS 1.1
        boost::asio::ssl::context::single_dh_use |
        boost::asio::ssl::context::no_compression  // Prevent CRIME attacks
    );

    ctx_server.use_certificate_chain(
        boost::asio::buffer(cert.data(), cert.size()));

    ctx_server.use_private_key(
        boost::asio::buffer(key.data(), key.size()),
        boost::asio::ssl::context::file_format::pem);

    if (ssl_dh_params_path_.size() > 0) {
      std::string const dh = read_file_to_string(ssl_dh_params_path_);
      ctx_server.use_tmp_dh(
          boost::asio::buffer(dh.data(), dh.size()));
    }
  }

  // Configure SSL client settings based on RpcBuilder options
  if (cfg_.ssl_client_disable_verification) {
    std::cout << "SSL client verification disabled (for testing only)" << std::endl;
    ctx_client.set_verify_mode(ssl::verify_none);
  } else {
#ifdef _WIN32
    // On Windows, add system root certificates to the SSL context
    add_windows_root_certs(ctx_client);
#else
    // On other platforms, set default verification paths
    boost::system::error_code ec;
    ctx_client.set_default_verify_paths(ec);
    if (ec) {
      std::cerr << "Warning: Failed to set default SSL verification paths: "
                << ec.message() << std::endl;
    } else {
      std::cout << "SSL client verification paths set successfully." << std::endl;
    }
#endif // _WIN32
    if (!cfg_.ssl_client_self_signed_cert_path.empty()) {
      try {
        ctx_client.load_verify_file(cfg_.ssl_client_self_signed_cert_path);
        std::cout << "Loaded self-signed certificate for SSL client: " 
                  << cfg_.ssl_client_self_signed_cert_path << std::endl;
      } catch (const std::exception& ex) {
        std::cerr << "Warning: Failed to load self-signed certificate: " 
                  << ex.what() << std::endl;
      }
    }
    ctx_client.set_verify_mode(ssl::verify_peer);
  }

  impl::g_cfg = std::move(cfg_);
  impl::g_orb = new impl::RpcImpl(ioc);
  return impl::g_orb;
}

NPRPC_API uint32_t ObjectServant::release() noexcept
{
  if (static_cast<impl::PoaImpl*>(poa_.get())->get_lifespan() ==
      PoaPolicy::Lifespan::Persistent) {
    return 1;
  }

  // std::cout << "ObjectServant::release() called for object with ID: "<< object_id_ <<
    // "\n ref_cnt: " << ref_cnt_.load() <<
    // "\n class_id: " << get_class() << std::endl;

  assert(is_unused() == false);

  auto cnt = ref_cnt_.fetch_sub(1, std::memory_order_acquire) - 1;
  if (cnt == 0) {
    static_cast<impl::PoaImpl*>(poa_.get())->deactivate_object(object_id_);
    impl::PoaImpl::delete_object(this);
  }

  return cnt;
}

NPRPC_API uint32_t Object::add_ref()
{
  auto const cnt = local_ref_cnt_.fetch_add(1, std::memory_order_release);
  if (policy_lifespan() == PoaPolicy::Lifespan::Persistent || cnt)
    return cnt + 1;

  flat_buffer buf;

  auto constexpr msg_size =
    sizeof(impl::Header) + sizeof(::nprpc::detail::flat::ObjectIdLocal);

  auto mb = buf.prepare(msg_size);
  buf.commit(msg_size);

  static_cast<impl::Header*>(mb.data())->size   = msg_size - 4;
  static_cast<impl::Header*>(mb.data())->msg_id = impl::MessageId::AddReference;
  static_cast<impl::Header*>(mb.data())->msg_type = impl::MessageType::Request;

  ::nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, sizeof(impl::Header));
  msg.object_id() = object_id();
  msg.poa_idx()   = poa_idx();

  nprpc::impl::g_orb->call_async(get_endpoint(), std::move(buf), std::nullopt);

  return cnt + 1;
}

NPRPC_API uint32_t Object::release()
{
  auto cnt = --local_ref_cnt_;
  if (cnt != 0) return cnt;

  if (::nprpc::impl::g_orb == nullptr) {
    delete this;
    return 0;
  }

  if (policy_lifespan() == PoaPolicy::Lifespan::Transient) {
    const auto& endpoint = get_endpoint();

    if (endpoint.type() == EndPointType::TcpTethered &&
        ::nprpc::impl::g_orb->has_session(endpoint) == false) {
      // session was closed and cannot be created.
    } else {
      flat_buffer buf;

      auto constexpr msg_size =
        sizeof(impl::Header) + sizeof(::nprpc::detail::flat::ObjectIdLocal);
      auto mb = buf.prepare(msg_size);
      buf.commit(msg_size);

      static_cast<impl::Header*>(mb.data())->size = msg_size - 4;
      static_cast<impl::Header*>(mb.data())->msg_id =
        impl::MessageId::ReleaseObject;
      static_cast<impl::Header*>(mb.data())->msg_type =
        impl::MessageType::Request;

      nprpc::detail::flat::ObjectIdLocal_Direct msg(buf, sizeof(impl::Header));
      msg.object_id() = object_id();
      msg.poa_idx()   = poa_idx();

      try {
        ::nprpc::impl::g_orb->call_async(
          endpoint,
          std::move(buf),
          [](const boost::system::error_code&, flat_buffer&) {
            // if (!ec) {
            // auto std_reply = nprpc::impl::handle_standart_reply(buf);
            // if (std_reply == false) {
            //	std::cerr << "received an unusual reply for function with no
            // output arguments" << std::endl;
            // }
            //}
          });
      } catch (Exception& ex) {
        std::cerr << ex.what() << '\n';
      }
    }
  }

  delete this;

  return 0;
}

NPRPC_API bool Object::select_endpoint(std::optional<EndPoint> remote_endpoint) noexcept
{
  try {
    std::string& urls = data_.urls;
    size_t start = [&urls, this, &remote_endpoint] {
      const auto same_machine = is_same_origin(impl::g_cfg.uuid);
      auto try_replace_ip = [&] (size_t pos, std::string_view prefix) {
        if (same_machine || !remote_endpoint )
          return;

        auto start = pos + prefix.length();
        auto end = urls.find(':', start);
        auto ipv4_str = urls.substr(start, end - start);

        boost::system::error_code ec;
        auto ipv4_addr = nprpc::impl::net::ip::make_address_v4(ipv4_str, ec);
        if ((!ec && ipv4_addr.to_uint() == 0x7F000001) || ipv4_str == "localhost") {
          // Change ip from localhost or 127.0.0.1 to ip of the remote endpoint
          auto remote_ip = remote_endpoint->hostname();
          assert(remote_ip.size() > 0 && "Remote endpoint must have a valid hostname");
          urls = urls.substr(0, start) + std::string(remote_ip) + urls.substr(end);
        }
      };

      size_t pos = std::string::npos, pos2 = std::string::npos;
      if (same_machine && ((pos = urls.find(mem_prefix)) != std::string::npos)) {
        // Prefer shared memory if possible
        return pos;
      }

      if ((pos = urls.find(tcp_prefix)) != std::string::npos)
        try_replace_ip(pos, tcp_prefix);
    
      if ((pos2 = urls.find(ws_prefix)) != std::string::npos)
        try_replace_ip(pos2, ws_prefix);

      if (pos != std::string::npos)
        return pos;

      if (pos2 != std::string::npos)
        return pos2;

      if ((pos = urls.find(wss_prefix)) == std::string::npos)
        throw std::runtime_error("No valid endpoint found for object " + class_id() +
                                " with urls: " + urls);

      return pos;
    } ();

    auto end = urls.find(';', start);
    auto size = (end != std::string::npos) ? end - start : std::string::npos;
    endpoint_ = EndPoint(urls.substr(start, size));
    return true;
  } catch (const std::exception& ex) {
    std::cerr << "Failed to select endpoint: " << ex.what() << '\n';
  }
  return false;
}

NPRPC_API uint32_t ObjectServant::add_ref() noexcept
{
  auto cnt = ref_cnt_.fetch_add(1, std::memory_order_release) + 1;

  //	if (cnt == 1 && static_cast<impl::PoaImpl*>(poa())->pl_lifespan ==
  // Policy_Lifespan::Transient) { 		std::lock_guard<std::mutex>
  // lk(impl::g_orb->new_activated_objects_mut_); 		auto& list =
  // impl::g_orb->new_activated_objects_; list.erase(std::find(begin(list),
  // end(list), this));
  //	}

  return cnt;
}

ReferenceList::ReferenceList() noexcept {
  impl_ = new impl::ReferenceListImpl();
}

ReferenceList::~ReferenceList() { 
  delete impl_;
}

void ReferenceList::add_ref(ObjectServant* obj) {
  impl_->add_ref(obj);
}

bool ReferenceList::remove_ref(poa_idx_t poa_idx, oid_t oid) {
  return impl_->remove_ref(poa_idx, oid);
}


Poa* PoaBuilder::build() {
  return static_cast<impl::RpcImpl*>(rpc_)->create_poa_impl(objects_max_, lifespan_policy_);
}

}  // namespace nprpc

#include <nprpc/serialization/nvp.hpp>
