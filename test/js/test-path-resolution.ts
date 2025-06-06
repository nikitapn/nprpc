// Test file to verify path alias resolution works correctly
import * as NPRPC from 'nprpc';

// This should work now that path aliases are resolved
console.log("NPRPC library imported successfully");

// Test that we can access the types from the generated files
// These types should now be properly resolved through relative paths
const test: NPRPC.oid_t = BigInt(123);
console.log("oid_t type accessible:", test);

export { NPRPC };
