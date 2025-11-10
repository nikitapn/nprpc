#!/usr/bin/env python3
"""
LSP Integration Test Runner
Runs all LSP integration tests and reports results
"""

import sys
import subprocess
import os
from pathlib import Path

def run_test(script_path, description):
    """Run a single test script and return success/failure"""
    print(f"\n{'='*60}")
    print(f"Running: {description}")
    print(f"Script:  {script_path.name}")
    print(f"{'='*60}")
    
    try:
        result = subprocess.run(
            [sys.executable, str(script_path)],
            cwd=script_path.parent,
            capture_output=True,
            text=True,
            timeout=10
        )
        
        if result.returncode == 0:
            print(f"✓ PASSED: {description}")
            if result.stdout:
                print(f"  Output: {result.stdout.strip()[:200]}")
            return True
        else:
            print(f"✗ FAILED: {description}")
            print(f"  Return code: {result.returncode}")
            if result.stdout:
                print(f"  stdout: {result.stdout}")
            if result.stderr:
                print(f"  stderr: {result.stderr}")
            return False
            
    except subprocess.TimeoutExpired:
        print(f"✗ TIMEOUT: {description}")
        return False
    except Exception as e:
        print(f"✗ ERROR: {description}")
        print(f"  Exception: {e}")
        return False

def main():
    # Get the script directory
    script_dir = Path(__file__).parent
    
    # Define tests to run (in order)
    tests = [
        ("test_diagnostics.py", "Parser error diagnostics"),
        ("test_diagnostic_ranges.py", "Diagnostic range accuracy"),
        ("test_goto_definition.py", "Go-to-definition on type names"),
        ("test_goto_alias.py", "Go-to-definition on type aliases"),
        ("test_param_goto.py", "Go-to-definition on function parameters"),
        ("test_semantic_tokens.py", "Semantic tokens generation"),
        ("test_lsp_features.py", "General LSP features (hover, etc.)"),
    ]
    
    print("NPIDL LSP Integration Test Suite")
    print("=" * 60)
    
    results = []
    for script_name, description in tests:
        script_path = script_dir / script_name
        
        if not script_path.exists():
            print(f"⚠ SKIP: {description} - script not found: {script_name}")
            results.append(None)
            continue
        
        success = run_test(script_path, description)
        results.append(success)
    
    # Print summary
    print("\n" + "=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)
    
    passed = sum(1 for r in results if r is True)
    failed = sum(1 for r in results if r is False)
    skipped = sum(1 for r in results if r is None)
    total = len(tests)
    
    print(f"Passed:  {passed}/{total}")
    print(f"Failed:  {failed}/{total}")
    print(f"Skipped: {skipped}/{total}")
    
    if failed > 0:
        print("\n❌ Some tests failed")
        return 1
    elif passed == 0:
        print("\n⚠️  No tests were run")
        return 1
    else:
        print("\n✅ All tests passed!")
        return 0

if __name__ == "__main__":
    sys.exit(main())
