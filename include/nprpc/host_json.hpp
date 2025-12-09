#pragma once

#include <nprpc/nprpc.hpp>
#include <nprpc/serialization/oarchive.h>

// Helper macros for variadic argument processing
#define FOR_EACH(macro, ...) FOR_EACH_IMPL(macro, __VA_ARGS__)
#define FOR_EACH_IMPL(macro, x, ...)                                           \
  macro(x) __VA_OPT__(FOR_EACH_IMPL(macro, __VA_ARGS__))

#define FOR_EACH_PARAM(macro, param, ...)                                      \
  FOR_EACH_PARAM_IMPL(macro, param, __VA_ARGS__)
#define FOR_EACH_PARAM_IMPL(macro, param, x, ...)                              \
  macro(param, x) __VA_OPT__(FOR_EACH_PARAM_IMPL(macro, param, __VA_ARGS__))

// Macro to declare an object in HostJson structure
#define DECLARE_HOST_OBJECT(name) nprpc::ObjectId name;

// Macro to generate serialization for an object
#define SERIALIZE_HOST_OBJECT(ar, name) ar& NVP(name);

// Macro to activate and register an object
#define ACTIVATE_HOST_OBJECT(host_json, poa, name, flags)                      \
  host_json.objects.name = poa->activate_object(name.get(), flags);            \
  spdlog::info(#name " - poa: {}, oid: {}", name->poa_index(), name->oid());

#define ACTIVATE_HOST_OBJECT_PTR(host_json, poa, name, flags)                  \
  host_json.objects.name = poa->activate_object(name, flags);                  \
  spdlog::info(#name " - poa: {}, oid: {}", name->poa_index(), name->oid());

#define ACTIVATE_HOST_OBJECT_REF(host_json, poa, name, flags)                  \
  host_json.objects.name = poa->activate_object(&name, flags);                 \
  spdlog::info(#name " - poa: {}, oid: {}", name->poa_index(), name->oid());

// Macro to generate the complete HostJson structure with multiple objects
#define DEFINE_HOST_JSON_STRUCT(...)                                           \
  struct HostJson {                                                            \
    bool secured;                                                              \
                                                                               \
    struct {                                                                   \
      FOR_EACH(DECLARE_HOST_OBJECT, __VA_ARGS__)                               \
                                                                               \
      template <typename Archive> void serialize(Archive& ar)                  \
      {                                                                        \
        FOR_EACH_PARAM(SERIALIZE_HOST_OBJECT, ar, __VA_ARGS__)                 \
      }                                                                        \
    } objects;                                                                 \
                                                                               \
    template <typename Archive> void serialize(Archive& ar)                    \
    {                                                                          \
      ar& NVP(secured);                                                        \
      ar& NVP(objects);                                                        \
    }                                                                          \
  };

#define SAVE_HOST_JSON_TO_FILE(host_json, http_dir)                            \
  do {                                                                         \
    std::ofstream os(fs::path(http_dir) / "host.json");                        \
    nprpc::serialization::json_oarchive oa(os);                                \
    oa << host_json;                                                           \
  } while (0)

/* Without macros, for reference:
#pragma once

#include <nprpc/nprpc.hpp>
#include <nprpc/serialization/oarchive.h>
#include <memory>
#include <tuple>
#include <string_view>

namespace nprpc::util {

// Template-based HostJson generator
template<typename... ObjectTypes>
class HostJsonBuilder {
private:
  std::tuple<std::shared_ptr<ObjectTypes>...> objects_;
  std::tuple<std::string_view...> object_names_;
  bool secured_ = false;

public:
  // Constructor takes object names as template arguments
  template<std::size_t... Names>
  explicit HostJsonBuilder(bool secured, std::string_view
(&names)[sizeof...(ObjectTypes)]) : secured_(secured) {
    static_assert(sizeof...(ObjectTypes) == sizeof...(Names), "Number of objects
must match number of names"); object_names_ = std::make_tuple(names...);
  }

  // Set objects
  template<std::size_t Index>
  void set_object(std::shared_ptr<std::tuple_element_t<Index,
std::tuple<ObjectTypes...>>> obj) { std::get<Index>(objects_) = obj;
  }

  // Generate and activate all objects
  std::string generate_host_json(nprpc::Poa* poa, uint32_t flags) {
    std::ostringstream oss;
    nprpc::serialization::json_oarchive oa(oss);

    // Create the JSON structure
    oa.start_object();
    oa.key("secured");
    oa.value(secured_);
    oa.key("objects");
    oa.start_object();

    // Activate and serialize each object
    activate_objects<0>(oa, poa, flags);

    oa.end_object();
    oa.end_object();

    return oss.str();
  }

private:
  template<std::size_t Index>
  void activate_objects(nprpc::serialization::json_oarchive& oa, nprpc::Poa*
poa, uint32_t flags) { if constexpr (Index < sizeof...(ObjectTypes)) { auto& obj
= std::get<Index>(objects_); auto& name = std::get<Index>(object_names_);

      if (obj) {
        auto oid = poa->activate_object(obj.get(), flags);
        oa.key(name);
        oa << oid;
      }

      activate_objects<Index + 1>(oa, poa, flags);
    }
  }
};

// Factory function for easier usage
template<typename... ObjectTypes>
auto make_host_json_builder(bool secured, std::array<std::string_view,
sizeof...(ObjectTypes)> names) { return HostJsonBuilder<ObjectTypes...>(secured,
names);
}

// Simplified macro for common usage patterns
#define NPRPC_HOST_JSON_BUILDER(secured, ...) \
  nprpc::util::make_host_json_builder<__VA_ARGS__>(secured, {#__VA_ARGS__})

} // namespace nprpc::util
*/