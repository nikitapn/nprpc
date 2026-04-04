#include <nprpc/impl/misc/thread_identity.hpp>

#include <atomic>

#if defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

thread_local pid_t thread_id = 0;
thread_local std::string thread_name;
std::atomic<uint32_t> global_counter{1};

uint32_t ensure_thread_registered()
{
  if (thread_id != 0)
    return thread_id;
#if defined(__APPLE__)
  uint64_t tid;
  pthread_threadid_np(nullptr, &tid);
  return thread_id = tid;
#elif defined(__linux__)
  return thread_id = syscall(SYS_gettid);
#else
  auto thread_id = global_counter.fetch_add(1, std::memory_order_relaxed);
  return thread_id;
#endif
}

} // namespace

namespace nprpc::impl {
NPRPC_API uint32_t get_thread_id()
{
  return ensure_thread_registered();
}

NPRPC_API std::string get_thread_name()
{
  return thread_name;
}

NPRPC_API const char* get_thread_name_cstr()
{
  return thread_name.c_str();
}

NPRPC_API bool set_thread_name(const std::string& name)
{
  thread_name = name;

#ifdef __linux__
  int ret = pthread_setname_np(pthread_self(), name.c_str());
  if (ret != 0) return false;
#endif
  return true;
}
} // namespace nprpc::impl
