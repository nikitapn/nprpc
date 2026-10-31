#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509v3.h>
#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/bytestring.h>
#endif

namespace nprpc::impl {
// Handshakes run on MsQuic workers and an unauthenticated peer can start them,
// so the bundle is parsed once and again only when the file changes.
// An empty ca_file means system trust.  Returns null if it cannot be loaded.
inline std::shared_ptr<X509_STORE> quic_trust_store(const std::string& ca_file) noexcept
{
  struct Entry {
    std::filesystem::file_time_type modified;
    std::shared_ptr<X509_STORE> store;
  };
  static std::mutex mutex;
  static std::map<std::string, Entry> stores;
  try {
    std::filesystem::file_time_type modified{};
    if (!ca_file.empty()) {
      std::error_code error;
      modified = std::filesystem::last_write_time(ca_file, error);
      if (error) return nullptr;
    }
    std::lock_guard lock(mutex);
    auto it = stores.find(ca_file);
    if (it != stores.end() && it->second.modified == modified) return it->second.store;
    std::shared_ptr<X509_STORE> store(X509_STORE_new(), X509_STORE_free);
    if (!store) return nullptr;
    if (ca_file.empty()) {
      if (X509_STORE_set_default_paths(store.get()) != 1) return nullptr;
    } else if (X509_STORE_load_locations(store.get(), ca_file.c_str(), nullptr) != 1) {
      return nullptr;
    }
    stores[ca_file] = {modified, store};
    return store;
  } catch (...) {
    return nullptr;
  }
}

// Portable DER certificates keep OpenSSL objects from MsQuic's TLS backend
// out of our crypto library. Built-in validation remains enabled as well.
inline bool verify_quic_peer(const QUIC_CONNECTION_EVENT& event,
                            const std::string& ca_file,
                            const std::string& server_name) noexcept
{
  try {
    const auto* leaf = static_cast<const QUIC_BUFFER*>(event.PEER_CERTIFICATE_RECEIVED.Certificate);
    if (!leaf || !leaf->Buffer || !leaf->Length) return false;
    auto* data = leaf->Buffer;
    const unsigned char* cursor = data;
    std::unique_ptr<X509, decltype(&X509_free)> cert(d2i_X509(nullptr, &cursor, leaf->Length), X509_free);
    auto store = quic_trust_store(ca_file);
    std::unique_ptr<X509_STORE_CTX, decltype(&X509_STORE_CTX_free)> ctx(X509_STORE_CTX_new(), X509_STORE_CTX_free);
    if (!cert || !store || !ctx) return false;

    auto free_chain = [](STACK_OF(X509)* chain) { sk_X509_pop_free(chain, X509_free); };
    std::unique_ptr<STACK_OF(X509), decltype(free_chain)> chain(sk_X509_new_null(), free_chain);
    if (!chain) return false;
    const auto* encoded_chain = static_cast<const QUIC_BUFFER*>(event.PEER_CERTIFICATE_RECEIVED.Chain);
    if (encoded_chain && encoded_chain->Buffer && encoded_chain->Length) {
#ifdef OPENSSL_IS_BORINGSSL
      CBS cbs;
      CBS_init(&cbs, encoded_chain->Buffer, encoded_chain->Length);
      if (!PKCS7_get_certificates(chain.get(), &cbs)) return false;
#else
      const unsigned char* p = encoded_chain->Buffer;
      std::unique_ptr<PKCS7, decltype(&PKCS7_free)> p7(d2i_PKCS7(nullptr, &p, encoded_chain->Length), PKCS7_free);
      if (!p7 || !PKCS7_type_is_signed(p7.get()) || !p7->d.sign) return false;
      for (int i = 0; i < sk_X509_num(p7->d.sign->cert); ++i) {
        X509* item = sk_X509_value(p7->d.sign->cert, i);
        if (!X509_up_ref(item)) return false;
        if (!sk_X509_push(chain.get(), item)) { X509_free(item); return false; }
      }
#endif
    }
    if (X509_STORE_CTX_init(ctx.get(), store.get(), cert.get(), chain.get()) != 1) return false;
    if (X509_STORE_CTX_set_purpose(ctx.get(), server_name.empty() ? X509_PURPOSE_SSL_CLIENT : X509_PURPOSE_SSL_SERVER) != 1)
      return false;
    if (!server_name.empty()) {
      auto* param = X509_STORE_CTX_get0_param(ctx.get());
      boost::system::error_code error;
      boost::asio::ip::make_address(server_name, error);
      if (!error) {
        if (X509_VERIFY_PARAM_set1_ip_asc(param, server_name.c_str()) != 1) return false;
      } else {
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (X509_VERIFY_PARAM_set1_host(param, server_name.c_str(), server_name.size()) != 1) return false;
      }
    }
    return X509_verify_cert(ctx.get()) == 1;
  } catch (...) {
    return false;
  }
}
}
