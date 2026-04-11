---
name: npidl-codegen
description: 'npidl compiler and C++ stub generation nuances. Use when: adding new IDL constructs to npidl, debugging generated C++ code, working on cpp_builder.cpp, fixing stream codegen, fixing parser token handling, or understanding why generated Direct-type accessors use () vs direct field access.'
---

# npidl Codegen — Knowledge Base

## 1. Parser: `peek()` Advances Lookahead — Do Not Use for Conditional Tests

`peek()` in the npidl parser is **not** a pure lookahead. It shifts the
lexer's internal state; the peeked token is effectively consumed from the
front of the stream.

### Rule

| Goal | Use |
|---|---|
| Test a token **and** consume it | `check(&Parser::one, TokenId::Foo)` |
| Test a token **without** consuming it | `PeekGuard` (saves/restores lookahead state) |
| **Never** use `peek()` alone for a conditional | — |

### Example (the `stream<direct T>` bug)

```cpp
// WRONG — peek() leaves the token partially consumed:
if (peek() == TokenId::OutDirect) { fn->direct = true; }

// CORRECT — check() tests and properly consumes:
if (check(&Parser::one, TokenId::OutDirect)) { fn->direct = true; }
```

### Example (variant arm parsing)

Mixing `peek()` + `match()` corrupts `tokens_looked_` and causes subsequent
`type_decl` calls to over-read or find wrong tokens.

```cpp
// WRONG — match() pops the queue but tokens_looked_ is not decremented,
// so the following check(&Parser::type_decl,...) mis-reads the next arm:
Token arm_tok = match(TokenId::Identifier);
match(':');
if (!check(&Parser::type_decl, std::ref(arm_type))) ...  // Wrong token!

// CORRECT — use a pure peek-chain; type_decl1's flush() consumes
// arm_tok, ':', and the type name all at once:
Token arm_tok = peek();                   // stays queued
if (peek() != TokenId::Colon) ...        // stays queued  
if (!check(&Parser::type_decl, ...)) ... // flush() consumes all 3
```

---

## 2. C++ Direct-Type Accessor: `top_object` Flag

Generated flat accessors (`Foo_Direct`) wrap data in `flat_buffer` at an
offset. When a Direct type is the **outermost** object (root of the
deserialization), access its fields directly:

```cpp
FooFlat_Direct __d(buf, 0);
__d.field_a();      // top_object = true  ✓
__d().field_a();    // top_object = false ✗ (extra operator() is wrong at root)
```

### In `assign_from_flat_type` / `assign_from_cpp_type`

Both functions take a `bool top_object` (or `bool top_type`) parameter:

- `true`  → emit `__d.field()` — use when `__d` is the root Direct object
- `false` → emit `__d().field()` — use for nested Direct fields

### When to set `top_object = true`

- `emit_stream_deserialize`: `__d` is declared as `AAA_Direct __d(buf, 0)` —
  the struct is the root, so `top_object = true`.
- `emit_stream_serialize`: `__d` is declared as `AAA_Direct __d(buf, 0)` —
  same rule, `top_type = true`.

---

## 3. Stream Codec Specializations Must Go Outside the IDL Namespace

`nprpc_stream::deserialize<T>` and `nprpc_stream::serialize<T>` are
specializations of templates defined in `namespace nprpc_stream`. They
**cannot** be emitted inside the IDL namespace (`namespace nprpc::foo`).

### Correct placement — in `finalize()`

```
} // module nprpc::foo     ← close IDL namespace first
namespace nprpc_stream {
  template<> inline ::nprpc::foo::Bar deserialize<::nprpc::foo::Bar>(...) { ... }
}
namespace nprpc_stream {
  template<> inline ::nprpc::flat_buffer serialize<::nprpc::foo::Bar>(...) { ... }
}
#endif
```

### Implementation pattern in `cpp_builder.cpp`

1. In `emit_interface()`: instead of calling `emit_stream_deserialize/serialize`
   inline, **push** the function node into `stream_codec_fns_`:
   ```cpp
   stream_codec_fns_.push_back(fn);
   ```
2. In `finalize()`: after writing `} // module ...`, iterate and emit:
   ```cpp
   for (auto* fn : stream_codec_fns_) {
     emit_stream_deserialize(fn);
     emit_stream_serialize(fn);
   }
   ```

---

## 4. Fully-Qualified Type Names in Generated Specializations

When emitting codec specializations, the builder's namespace context may be
inside an IDL module. Unqualified type names will fail to compile.

### Fix

Before calling any type-name emitting helper inside `emit_stream_deserialize`
or `emit_stream_serialize`, set:

```cpp
always_full_namespace(true);   // save the setting beforehand with bd = namespace_depth_
```

And restore afterwards:

```cpp
always_full_namespace(false);  // or restore saved bd
```

This ensures all emitted type references are `::nprpc::foo::Bar` not just `Bar`.

---

## 5. `StreamWriter<T>` Complex Type Send Path

`StreamWriter<T>` had a `static_assert` gating complex (non-trivially-copyable)
types. The correct approach is to call the codec:

```cpp
// In StreamWriter::resume(), complex-type branch:
auto __buf = nprpc_stream::serialize<T>(coro_.promise().current_value_);
auto __span = __buf.data();
coro_.promise().manager_->send_chunk(
    stream_id_,
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(__span.data()), __span.size()),
    sequence_++);
```

Primary template declarations (undefined, just declared) must exist in both
`stream_reader.hpp` and `stream_writer.hpp` so specializations in the
generated header can be found:

```cpp
// stream_reader.hpp
namespace nprpc_stream {
  template<typename T> T deserialize(::nprpc::flat_buffer& buf);  // defined by generated specialization
}

// stream_writer.hpp
namespace nprpc_stream {
  template<typename T> ::nprpc::flat_buffer serialize(const T& value);  // defined by generated specialization
}
```

---

## 7. Variant Direct Accessor: `Kind` Enum Lives Outside `namespace flat`

The `flat::TestVariant_Direct` class is emitted **inside** `namespace flat {}`.
The `Kind` enum is part of the **outer** non-flat type (`nprpc::test::TestVariant::Kind`).
Referencing it as `TestVariant::Kind` inside `namespace flat` resolves to
`flat::TestVariant::Kind` which does **not** exist.

### Fix

Make `kind()` return `std::uint32_t` and accept `std::uint32_t` in the setter:

```cpp
// Inside namespace flat — CORRECT
std::uint32_t kind() const noexcept { return base().kind; }
void set_kind(std::uint32_t k) noexcept { base().kind = k; }
```

When calling from outside `namespace flat` (e.g., in `assign_from_flat_type`),
cast to the enum before the switch, and cast to `uint32_t` before the setter:

```cpp
// assign_from_flat_type — switch needs enum type
switch (static_cast<nprpc::test::TestVariant::Kind>(vd.kind())) { ... }

// assign_from_cpp_type — setter needs uint32_t
vd.set_kind(static_cast<std::uint32_t>(src.payload.kind));
```


After `QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE`, MsQuic requires the app to call
`StreamClose(stream)` to release the handle. Omitting this keeps the
connection's rundown reference alive.

**Symptom**: `MsQuicRegistrationClose` blocks forever during `QuicApi`
static-destructor teardown → process hangs after all tests pass (exit code
124 from timeout).

```cpp
case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: {
  data_streams_.erase(stream);               // remove from tracking map
  QuicApi::instance().api()->StreamClose(stream);  // REQUIRED — releases handle
  break;
}
```

This applies to **all** stream types: main RPC stream, data streams (server
and client side).
