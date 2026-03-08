---
name: build-and-test-nprpc
description: 'Build and test NPRPC safely using the unified runner, convenience scripts, and focused manual repro only when needed. Use when changing C++, IDL/codegen, JS/TS bindings, Swift bindings, or when builds/tests fail and you need to avoid stale generated outputs, missed copy steps, or mismatched Swift Docker artifacts.'
argument-hint: 'Describe what changed, which surface is affected (cpp/js/swift/codegen), and whether you need normal verification or debugger-focused repro'
user-invocable: true
disable-model-invocation: false
---

# Build And Test NPRPC

## When To Use

- Verifying NPRPC changes across C++, JS/TS, and Swift.
- Working on IDL, generators, generated stubs, or runtime serialization.
- Deciding whether to use `run_all_tests.py` or a manual convenience script.
- Debugging why a targeted build or test passed while the real workflow still fails.
- Avoiding stale generated outputs in `test/js/src/gen` or Swift test/generated directories.

## Repo-Specific Facts

- `run_all_tests.py` is a good default reminder when a change may affect more than one surface or when you want stage-by-stage failure output.
- The unified runner prints stage names and surfaces build/test output on failure, which is often the fastest path to a correct diagnosis.
- Some partial build flows regenerate files only in the CMake build tree and do not automatically sync them into the JS or Swift directories consumed by tests.
- `npm test` in `test/js` does not guarantee that generated TypeScript stubs have been refreshed from the build output.
- Swift tests depend on the Docker-built `libnprpc.so` in `.build_ubuntu_swift`; running `docker-build-swift.sh --test` alone after core C++ API changes can test against a stale library.
- If you changed core C++ interfaces used by Swift, the Swift-targeted library must be rebuilt first; the unified runner already handles that path.

## Default Procedure

### 1. Consider The Unified Runner First

Use `run_all_tests.py` as a friendly default when:

- the change may affect more than one language surface
- you touched IDL or generators
- you want stage-by-stage output instead of guessing which layer is stale
- a manual script already gave a confusing or incomplete result

Useful invocations:

- Full verification:
  - `/bin/python run_all_tests.py`
- Swift-only verification:
  - `/bin/python run_all_tests.py --skip-cmake --skip-cpp --skip-js`
- JS-only verification:
  - `/bin/python run_all_tests.py --skip-cmake --skip-cpp --skip-swift`
- C++-only verification after a build already exists:
  - `/bin/python run_all_tests.py --skip-cmake --skip-js --skip-swift`

Prefer the smallest skip set that still exercises the surface you changed.

### 2. Treat Codegen And IDL Changes As Multi-Surface By Default

If you touched any of these:

- `idl/**`
- `npidl/src/**`
- generated object serialization/runtime code
- shared transport or `ObjectId` handling

then assume more than one language surface may need regeneration or rebuild.

Do not rely only on a narrow local test first. Use the unified runner or another path that you know includes regeneration and any needed sync/copy behavior.

### 3. Only Use Manual Scripts For Focused Repro Or Debugging

Use convenience scripts when you already know which layer is failing and want a smaller reproduction loop.

Examples:

- `cmake --build <build-dir> --target <target>` for focused C++ rebuilds
- `.build_relwith_debinfo/test/nprpc_test --gtest_filter=...` for focused C++ tests
- `cd test/js && npm test` for JS repro after generation is already known-good
- `cd nprpc_swift && ./docker-build-swift.sh --test` for Swift repro after the Swift-targeted NPRPC library is already rebuilt

Manual scripts are often fine for focused verification. The main caution is to avoid assuming they refreshed every generated or copied artifact that the language-specific tests consume.

## Decision Points

### If You Changed Only C++ Runtime Or Tests

- Use the unified runner with JS and Swift skipped if the change cannot affect generated code or language bindings.
- For tight loops, use a focused C++ binary after one unified or full build-backed validation pass.

### If You Changed IDL Or Generators

- Keep the unified runner in mind early.
- Assume JS and Swift consumers may become stale even if a local C++ build succeeds.
- Do not trust `npm test` or `swift test` alone until generation and copy/sync steps are known-good.

### If You Changed JS Runtime Or JS Tests Only

- The unified runner with C++ and Swift skipped is a good default if you want the repo-standard flow.
- If you run `npm test` manually, first make sure generated JS test stubs are current.

### If You Changed Swift Wrapper, Bridge, Or Swift Tests

- The unified runner’s Swift path is the easiest way to avoid stale Docker-built artifacts.
- If core C++ code changed too, do not run only `docker-build-swift.sh --test`; rebuild `libnprpc.so` for the Swift Docker toolchain first, or use the unified runner.

## Generator And Copy Nuances

### JS / TS

- A partial CMake build can leave `test/js/src/gen` stale.
- `npm test` validates the JS workspace state, not the CMake build tree.
- If JS tests suddenly fail after generator or IDL work, suspect stale copied stubs before blaming the runtime.

### Swift

- Swift test stubs are refreshed by `nprpc_swift/gen_stubs.sh`.
- The Swift package links against `.build_ubuntu_swift/libnprpc.so`, not the normal host build output.
- After changing core C++ APIs or virtual methods, rebuild the Swift-targeted NPRPC library before trusting Swift test results.

## Debugging Procedure

### 1. Use The Unified Runner When The Failing Stage Is Not Obvious

Use the runner to determine which stage actually fails:

- CMake build
- C++ tests
- JS build/tests
- Swift Docker build
- Swift tests

This helps prevent wasting time debugging the wrong environment.

### 2. Then Switch To A Focused Repro

Once the failing stage is known, shrink the loop:

- C++: focused `nprpc_test` filter or direct test binary
- JS: `npm run build` / `npm test` in `test/js`
- Swift: `docker-build-nprpc.sh` then `docker-build-swift.sh --test`

### 3. Preserve Environment Assumptions

Remember the environment used by the failing stage:

- Swift runs inside Docker with `.build_ubuntu_swift`
- JS tests may rely on generated files already copied into `test/js/src/gen`
- C++ tests may launch side processes such as `npnameserver`

### 4. If GDB Is Needed

Use GDB only after you already know the exact failing binary and setup.

Common rules:

- Reproduce with the same build artifacts that failed under the runner.
- For C++ tests, use the direct binary and any needed `--gtest_filter`.
- Clean up leftover helper processes such as `npnameserver` or test servers before re-running.
- Do not try to debug a Swift failure from the host environment if the real failure occurs in the Docker Swift toolchain.

## Completion Checks

Do not consider verification complete until all of these are true:

- The relevant `run_all_tests.py` stage or skip-set passes.
- You have ruled out stale generated outputs for JS and Swift when codegen or IDL changed.
- Manual repro, if used, matches the environment of the failing stage.
- If Swift touches core C++ APIs, the Swift-targeted Docker library was rebuilt.
- If you used a focused script instead of the unified runner for the final claim, you can explain why that was sufficient.

## Common Failure Patterns

- `npm test` fails after generator or IDL changes even though C++ built fine.
  - Suspect stale copied TS stubs.

- `docker-build-swift.sh --test` fails after C++ API changes but the normal host build is green.
  - Suspect stale `.build_ubuntu_swift/libnprpc.so`.

- A narrow CMake target passes but JS or Swift still use older generated files.
  - Suspect missing sync/copy steps into language-specific directories.

- GDB repro does not match the failure seen in automation.
  - Suspect the wrong binary, wrong build dir, missing helper processes, or wrong Docker vs host environment.

## Recommended Commands

- Full runner:
  - `/bin/python run_all_tests.py`
- Swift only:
  - `/bin/python run_all_tests.py --skip-cmake --skip-cpp --skip-js`
- JS only:
  - `/bin/python run_all_tests.py --skip-cmake --skip-cpp --skip-swift`
- Focused C++ test:
  - `./.build_relwith_debinfo/test/nprpc_test --gtest_filter=...`
- Swift rebuild when manually bypassing the unified runner:
  - `cd nprpc_swift && ./docker-build-nprpc.sh`
  - `cd nprpc_swift && ./docker-build-swift.sh --test`
