# NPIDL Test Suite

This directory contains tests for the NPIDL compiler and LSP server.

## Test Structure

### Unit Tests (`src/`)
- C++ unit tests using GoogleTest
- Tests for parser, AST, type checking, code generation
- Run with: `cmake --build build --target npidl_test`

### Integration Tests (`intergration_tests/`)
- Python-based LSP protocol integration tests
- Test the actual LSP server executable end-to-end
- Validate real client-server communication

## Running Tests

### All Tests
```bash
cd build/linux
ctest --output-on-failure
```

### Unit Tests Only
```bash
cd build/linux
ctest -R npidl_test --output-on-failure
```

### LSP Integration Tests Only
```bash
cd build/linux
ctest -R npidl_lsp_integration_tests --output-on-failure
# Or use the helper target:
make run_lsp_tests
```

### Individual LSP Test Scripts
```bash
cd nprpc/npidl/test/integration_tests
python3 test_diagnostics.py
python3 test_goto_definition.py
python3 test_semantic_tokens.py
# etc.
```

## LSP Integration Tests

The LSP integration tests verify:

1. **Diagnostics** (`test_diagnostics.py`, `test_diagnostic_ranges.py`)
   - Parser error reporting
   - Error position accuracy
   - Diagnostic range calculation

2. **Go-to-Definition** (`test_goto_definition.py`, `test_goto_alias.py`, `test_param_goto.py`)
   - Jump to type definitions
   - Type alias resolution
   - Function parameter type navigation
   - Type reference vs definition detection

3. **Semantic Tokens** (`test_semantic_tokens.py`)
   - Token type classification
   - Token position accuracy
   - Delta encoding validation

4. **Hover** (`test_lsp_features.py`)
   - Type information display
   - Documentation formatting

## Test Organization

### Why Integration Tests?

The LSP tests are integration tests (not unit tests) because they:
- Launch the actual `npidl --lsp` executable
- Test real JSON-RPC communication over stdin/stdout
- Verify the complete LSP protocol implementation
- Catch issues that unit tests might miss (serialization, buffering, protocol errors)

### Adding New Tests

1. Create a new Python script in `integration_tests/`
2. Use the existing test scripts as templates
3. Add the test to `run_lsp_tests.py`
4. The test will automatically run with `ctest`

Example test structure:
```python
#!/usr/bin/env python3
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from lsp_test_base import LspTestClient

def main():
    client = LspTestClient()
    # ... test code ...
    assert condition, "Error message"
    print("✓ Test passed")
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

## Debugging

### Debug LSP Server
```bash
# Server logs go to stderr
/path/to/npidl --lsp 2>lsp_debug.log

# Or use the debug scripts:
python3 debug_stderr.py  # Shows stderr output
python3 debug_index.py   # Dumps position index
```

### Verbose Test Output
```bash
cd build/linux
ctest -R npidl_lsp_integration_tests -V
```

## Test Files

- `*.npidl` - Test input files with various language features
- `test_*.py` - Individual test scripts
- `run_lsp_tests.py` - Main test runner
- `debug_*.py` - Debugging utilities
- `*.sh` - Shell script tests (legacy)
