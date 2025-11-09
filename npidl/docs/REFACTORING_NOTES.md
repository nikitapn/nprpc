# TypeScript Builder Refactoring Progress

## Goal
Refactor the TypeScript code generator to use simple marshal/unmarshal functions instead of complex wrapper objects (`_Direct` classes), making the generated code:
- More maintainable
- Easier to debug
- Faster (potentially inlineable by Google Closure Compiler)
- Using TypedArrays for efficient memory access (Emscripten-style)

## Current State

### Completed
1. ✅ Added HeapViews class generation in `emit_heap_views()`
   - Provides Emscripten-style HEAP8, HEAP16, HEAP32, etc. accessors
   - Clean TypedArray access to ArrayBuffer

2. ✅ Added `emit_marshal_function()` and `emit_unmarshal_function()`
   - Generates marshal_StructName() functions to serialize plain objects
   - Generates unmarshal_StructName() functions to deserialize to plain objects
   - Handles all field types: fundamental, enum, string, struct, object, vector, array, optional, alias

3. ✅ Added `emit_field_marshal()` and `emit_field_unmarshal()` helper methods
   - Per-field marshalling logic
   - Proper alignment handling
   - Recursive handling of nested structs

4. ✅ Modified `emit_struct()` to emit marshal/unmarshal alongside existing code

5. ✅ Created `/home/nikita/projects/npsystem/nprpc/nprpc_js/src/marshal_helpers.ts` with runtime helpers

### Current Issues
1. The HeapViews approach conflicts with existing FlatBuffer infrastructure
2. Marshal helpers need access to FlatBuffer for allocation (_alloc function)
3. Generated code calls heap/buffer functions that don't quite match

## Next Steps

### Option A: Full Refactoring (Aggressive)
1. Remove all `_Direct` class generation completely
2. Update `emit_interface()` to use marshal/unmarshal in proxy functions
3. Update servant dispatch to use marshal/unmarshal
4. Ensure FlatBuffer integration works with HeapViews
5. Test thoroughly with existing code

### Option B: Hybrid Approach (Conservative)
1. Keep existing `_Direct` classes for backwards compatibility
2. Add marshal/unmarshal as alternative API
3. Add a flag to switch between old and new generation
4. Gradually migrate code to use new approach
5. Remove old code once fully validated

### Option C: Simplified Refactoring (Recommended)
1. Keep DataView approach for now (don't introduce HeapViews)
2. Generate marshal/unmarshal functions that work with existing FlatBuffer
3. Simplify proxy/servant code to use these functions instead of _Direct classes
4. Less disruptive, easier to test
5. Can still optimize to TypedArrays later

## Recommendation

I recommend **Option C** - a simplified refactoring that:
- Generates marshal/unmarshal functions working with DataView (existing infrastructure)
- Updates proxy and servant generation to use these functions
- Keeps changes minimal and testable
- Can be enhanced to use TypedArrays in a follow-up iteration

This approach is less risky and allows incremental testing.

## Files Modified So Far
- `/home/nikita/projects/npsystem/nprpc/npidl/src/ts_builder.hpp` - Added new method declarations
- `/home/nikita/projects/npsystem/nprpc/npidl/src/ts_builder.cpp` - Added implementation
- `/home/nikita/projects/npsystem/nprpc/nprpc_js/src/marshal_helpers.ts` - New runtime helpers (needs revision)

## Implementation Details Needed

### For marshal functions to work properly:
1. They need access to FlatBuffer for dynamic allocation
2. String/array marshalling requires _alloc() calls
3. Current signature: `marshal_X(heap: HeapViews, offset: number, data: X)`
4. Should be: `marshal_X(buf: FlatBuffer, offset: number, data: X)`

### Example of what generated code should look like:
```typescript
// Current (complex):
let _ = new Flat_ns.M1_Direct(buf, 32);
_._1 = obj.field1;
_._2 = obj.field2;

// Desired (simple):
marshal_M1(buf, 32, { field1: obj.field1, field2: obj.field2 });
```

## Build Status
- Code compiles with some errors related to HeapViews/FlatBuffer mismatch
- Needs adjustment to work with existing infrastructure

