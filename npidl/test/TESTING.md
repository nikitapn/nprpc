# LSP Integration Tests - Quick Reference

## Running Tests

### From Build Directory
```bash
cd build/linux
make npidl_test  # Runs all tests (unit + integration)
make run_lsp_tests  # Runs only LSP integration tests
```

### Using CTest
```bash
cd build/linux/nprpc/npidl/test
ctest --output-on-failure  # All tests
ctest -L lsp -V  # LSP tests with verbose output
ctest -R npidl_lsp_integration_tests  # LSP tests only
```

### Direct Python Execution
```bash
cd nprpc/npidl/test/integration_tests
python3 run_lsp_tests.py
python3 test_diagnostics.py  # Individual test
```

## Available Tests

| Test Script | What It Tests |
|------------|---------------|
| `test_diagnostics.py` | Parser error reporting |
| `test_diagnostic_ranges.py` | Error position accuracy |
| `test_goto_definition.py` | Jump to type definitions |
| `test_goto_alias.py` | Type alias resolution |
| `test_param_goto.py` | Function parameter navigation |
| `test_semantic_tokens.py` | Token classification |
| `test_lsp_features.py` | Hover and general LSP features |

## Test Output

✅ **Success**: All tests passed
```
Passed:  7/7
Failed:  0/7
✅ All tests passed!
```

❌ **Failure**: Some tests failed
```
Passed:  5/7
Failed:  2/7
❌ Some tests failed
```

## Debugging Failed Tests

1. **Run individual test**:
   ```bash
   python3 test_diagnostics.py
   ```

2. **Check LSP server logs**:
   ```bash
   python3 debug_stderr.py  # Shows server stderr
   ```

3. **Inspect position index**:
   ```bash
   python3 debug_index.py test_file.npidl
   ```

## CI/CD Integration

The tests run automatically on `make npidl_test`. Exit code:
- `0` = all tests passed
- `1` = some tests failed

Perfect for CI pipelines!
