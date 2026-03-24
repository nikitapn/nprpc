#include <linux/bpf.h>

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

struct reuseport_config {
  __u32 worker_count;
  __u32 scid_len;
};

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 1);
  __type(key, __u32);
  __type(value, struct reuseport_config);
} config_map SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_REUSEPORT_SOCKARRAY);
  __uint(max_entries, 161);
  __type(key, __u32);
  __type(value, __u64);
} reuseport_array SEC(".maps");

SEC("sk_reuseport")
int quic_select_reuseport(struct sk_reuseport_md* ctx)
{
  const __u32 key = 0;
  const __u8* data = (const __u8*)(long)ctx->data;
  const __u8* data_end = (const __u8*)(long)ctx->data_end;
  struct reuseport_config* cfg;
  __u32 hash = ctx->hash;
  __u32 selected;

  cfg = bpf_map_lookup_elem(&config_map, &key);
  if (!cfg || cfg->worker_count == 0) {
    return SK_DROP;
  }

  selected = hash % cfg->worker_count;

  if (data + 1 <= data_end) {
    if (data[0] & 0x80u) {
      if (data + 7 <= data_end) {
        const __u8 dcid_len = data[5];

        if (dcid_len != 0 && dcid_len <= 20) {
          hash = 2166136261u;
          hash ^= data[6];
          hash *= 16777619u;

          if (dcid_len > 1 && data + 8 <= data_end) {
            hash ^= data[7];
            hash *= 16777619u;
          }

          if (dcid_len > 2 && data + 9 <= data_end) {
            hash ^= data[8];
            hash *= 16777619u;
          }

          if (dcid_len > 3 && data + 10 <= data_end) {
            hash ^= data[9];
            hash *= 16777619u;
          }

          selected = hash % cfg->worker_count;
        }
      }
    } else if (cfg->scid_len != 0 && cfg->scid_len <= 20 &&
               data + 2 <= data_end) {
      selected = data[1] % cfg->worker_count;
    }
  }

  if (bpf_sk_select_reuseport(ctx, &reuseport_array, &selected, 0) == 0) {
    return SK_PASS;
  }

  return SK_DROP;
}

char LICENSE[] SEC("license") = "GPL";