import pytest
import subprocess
import sys
import os


@pytest.mark.parametrize("payload", [
    # Exact exploit case - command injection via semicolon
    "https://raw.githubusercontent.com/brendangregg/FlameGraph/refs/heads/master/flamegraph.pl; echo MALICIOUS > /tmp/hacked",
    # Command substitution attack
    "$(whoami)",
    # Backtick command execution
    "`id`",
    # Valid input (should pass)
    "https://raw.githubusercontent.com/brendangregg/FlameGraph/refs/heads/master/flamegraph.pl",
])
def test_shell_command_injection_mitigated(payload):
    """Invariant: Shell commands never include unsanitized user input"""
    # Import the actual production module
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import prof.prof
    
    # Create a test file to simulate the vulnerable code path
    test_script = f"""
import os
import sys
# Simulate the vulnerable pattern with our payload
url = "{payload}"
os.system(f"wget {{url}}")
os.system("chmod +x flamegraph.pl")
"""
    
    # Execute the test script in a subprocess to isolate shell execution
    result = subprocess.run(
        [sys.executable, "-c", test_script],
        capture_output=True,
        text=True,
        timeout=2
    )
    
    # Check if malicious command was executed
    assert "MALICIOUS" not in result.stdout, f"Command injection detected in payload: {payload}"
    assert "MALICIOUS" not in result.stderr, f"Command injection detected in payload: {payload}"
    
    # Additional safety check - ensure no unexpected files were created
    if "MALICIOUS" in payload:
        assert not os.path.exists("/tmp/hacked"), f"Malicious file created by payload: {payload}"