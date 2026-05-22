import pytest
import ctypes
import struct


# Simulated ARP/network descriptor and buffer management
# This models the vulnerable pattern from arp.c and vionet.c

class NetworkDescriptor:
    """Simulates a network descriptor with address, size, and offset fields."""
    def __init__(self, buffer_size, curr_offset):
        self.buffer = bytearray(buffer_size)
        self.buffer_size = buffer_size
        self.address = self.buffer
        self.curroffset = curr_offset

    def safe_memcpy(self, frame_data):
        """
        Safe implementation: validates that dest + copy_size stays within buffer.
        Returns True if copy succeeded, raises ValueError if bounds exceeded.
        """
        frame_size = len(frame_data)
        # Security invariant: curroffset + frame_size must not exceed buffer_size
        if self.curroffset < 0:
            raise ValueError(f"Negative offset {self.curroffset} is invalid")
        if self.curroffset > self.buffer_size:
            raise ValueError(
                f"Offset {self.curroffset} exceeds buffer size {self.buffer_size}"
            )
        if self.curroffset + frame_size > self.buffer_size:
            raise ValueError(
                f"Copy would overflow: offset({self.curroffset}) + "
                f"frame_size({frame_size}) = {self.curroffset + frame_size} "
                f"> buffer_size({self.buffer_size})"
            )
        # Perform the copy only if bounds are valid
        self.buffer[self.curroffset:self.curroffset + frame_size] = frame_data
        return True

    def unsafe_memcpy_check(self, frame_data):
        """
        Simulates the VULNERABLE pattern: no bounds check before copy.
        We detect what WOULD happen without validation.
        Returns True if it would overflow, False if safe.
        """
        frame_size = len(frame_data)
        would_overflow = (
            self.curroffset < 0 or
            self.curroffset > self.buffer_size or
            (self.curroffset + frame_size) > self.buffer_size
        )
        return would_overflow


# ARP frame size (28 bytes for standard ARP over Ethernet)
ARP_FRAME_SIZE = 28

# Typical network buffer sizes
TYPICAL_BUFFER_SIZE = 1500  # MTU
SMALL_BUFFER_SIZE = 64
EXACT_BUFFER_SIZE = ARP_FRAME_SIZE


@pytest.mark.parametrize("payload", [
    # (buffer_size, curr_offset, frame_size, description)
    # --- Overflow attempts ---
    (64, 60, ARP_FRAME_SIZE, "offset near end causes overflow"),
    (64, 64, ARP_FRAME_SIZE, "offset at exact buffer end causes overflow"),
    (64, 65, ARP_FRAME_SIZE, "offset beyond buffer end"),
    (64, 100, ARP_FRAME_SIZE, "offset far beyond buffer"),
    (64, 2**31 - 1, ARP_FRAME_SIZE, "large positive offset (int32 max)"),
    (64, 2**32 - 1, ARP_FRAME_SIZE, "offset at uint32 max"),
    (64, 2**63 - 1, ARP_FRAME_SIZE, "offset at int64 max"),
    (1500, 1499, ARP_FRAME_SIZE, "one byte before end with full frame"),
    (1500, 1500, ARP_FRAME_SIZE, "offset at MTU boundary"),
    (1500, 1501, ARP_FRAME_SIZE, "offset just past MTU"),
    (ARP_FRAME_SIZE, 1, ARP_FRAME_SIZE, "exact buffer, offset by 1"),
    (ARP_FRAME_SIZE, ARP_FRAME_SIZE, ARP_FRAME_SIZE, "offset equals buffer size"),
    # --- Negative offset attacks ---
    (-1, 0, ARP_FRAME_SIZE, "negative buffer size edge case"),
    (64, -1, ARP_FRAME_SIZE, "negative offset"),
    (64, -100, ARP_FRAME_SIZE, "large negative offset"),
    (64, -(2**31), ARP_FRAME_SIZE, "int32 min negative offset"),
    # --- Zero/empty edge cases ---
    (0, 0, ARP_FRAME_SIZE, "zero-size buffer"),
    (ARP_FRAME_SIZE, 0, 0, "zero-size frame copy"),
    # --- Integer overflow / wraparound ---
    (64, 2**64 - ARP_FRAME_SIZE + 1, ARP_FRAME_SIZE, "offset causes uint64 wraparound"),
    (64, 2**32 - ARP_FRAME_SIZE + 1, ARP_FRAME_SIZE, "offset causes uint32 wraparound"),
    # --- Valid boundary cases (should succeed) ---
    (64, 0, ARP_FRAME_SIZE, "valid: offset 0, fits exactly"),
    (64, 36, ARP_FRAME_SIZE, "valid: offset + frame fits exactly in 64-byte buffer"),
    (1500, 0, ARP_FRAME_SIZE, "valid: standard ARP in MTU buffer"),
    (1500, 1472, ARP_FRAME_SIZE, "valid: last valid position in MTU buffer"),
])
def test_arp_buffer_bounds_invariant(payload):
    """
    Invariant: A memcpy using descriptor offset must NEVER write beyond the
    allocated buffer boundary. The expression (address + curroffset + frame_size)
    must always be <= (address + buffer_size). Any operation that would violate
    this must be rejected before the copy occurs.
    """
    buffer_size, curr_offset, frame_size, description = payload

    # Create frame data of specified size
    frame_data = bytes([0xAA] * frame_size) if frame_size > 0 else b""

    # Determine if this operation SHOULD be allowed
    def is_valid_operation(buf_size, offset, f_size):
        if buf_size <= 0:
            return False
        if offset < 0:
            return False
        if offset > buf_size:
            return False
        if offset + f_size > buf_size:
            return False
        return True

    expected_valid = is_valid_operation(buffer_size, curr_offset, frame_size)

    if buffer_size <= 0:
        # Cannot create a meaningful buffer; operation must be rejected
        # The invariant: invalid buffer sizes must not allow any copy
        assert not expected_valid, (
            f"[{description}] Zero or negative buffer size must never allow a copy"
        )
        return

    # Create descriptor with valid buffer
    actual_buffer_size = max(1, buffer_size)  # ensure we can allocate
    desc = NetworkDescriptor(actual_buffer_size, curr_offset)

    if expected_valid:
        # INVARIANT: Valid operations must succeed without corruption
        try:
            result = desc.safe_memcpy(frame_data)
            assert result is True, f"[{description}] Valid operation should succeed"
            # Verify data integrity - no out-of-bounds write occurred
            if frame_size > 0:
                written = bytes(desc.buffer[curr_offset:curr_offset + frame_size])
                assert written == frame_data, (
                    f"[{description}] Data written must match source exactly"
                )
            # Verify no memory outside the intended region was modified
            if curr_offset > 0:
                before_region = bytes(desc.buffer[:curr_offset])
                assert before_region == bytes(curr_offset), (
                    f"[{description}] Memory before offset must not be modified"
                )
            end_pos = curr_offset + frame_size
            if end_pos < actual_buffer_size:
                after_region = bytes(desc.buffer[end_pos:])
                assert after_region == bytes(actual_buffer_size - end_pos), (
                    f"[{description}] Memory after copy region must not be modified"
                )
        except ValueError as e:
            pytest.fail(
                f"[{description}] Valid operation was incorrectly rejected: {e}"
            )
    else:
        # INVARIANT: Invalid/overflow operations must be rejected
        # The unsafe pattern WOULD overflow - verify detection
        would_overflow = desc.unsafe_memcpy_check(frame_data)
        assert would_overflow, (
            f"[{description}] Overflow detection failed: "
            f"offset={curr_offset}, frame_size={frame_size}, "
            f"buffer_size={actual_buffer_size} should be detected as overflow"
        )

        # The safe implementation must raise an exception
        with pytest.raises(ValueError, match=r"(overflow|exceeds|Negative|invalid)"):
            desc.safe_memcpy(frame_data)

        # CRITICAL: Buffer must remain unmodified after rejected operation
        original_buffer = bytes(actual_buffer_size)
        assert bytes(desc.buffer) == original_buffer, (
            f"[{description}] Buffer must not be modified when operation is rejected"
        )


@pytest.mark.parametrize("attack_scenario", [
    # Simulate vionet.c:121 style copy without size validation
    {"desc_buffer_size": 256, "copy_offset": 200, "copy_size": 100,
     "label": "vionet partial overflow"},
    {"desc_buffer_size": 256, "copy_offset": 255, "copy_size": 28,
     "label": "vionet single-byte-before-end overflow"},
    {"desc_buffer_size": 1500, "copy_offset": 0, "copy_size": 1501,
     "label": "vionet copy exceeds MTU"},
    {"desc_buffer_size": 1500, "copy_offset": 1000, "copy_size": 501,
     "label": "vionet offset+size exactly overflows"},
    {"desc_buffer_size": 64, "copy_offset": 0, "copy_size": 65,
     "label": "vionet one byte overflow"},
])
def test_vionet_copy_bounds_invariant(attack_scenario):
    """
    Invariant: Network copy operations (vionet.c pattern) must validate that
    desc.address + copy_size does not exceed the allocated buffer. Any copy
    that would write past the buffer end must be rejected.
    """
    buf_size = attack_scenario["desc_buffer_size"]
    offset = attack_scenario["copy_offset"]
    copy_size = attack_scenario["copy_size"]
    label = attack_scenario["label"]

    # The invariant: offset + copy_size must never exceed buffer_size
    would_overflow = (offset + copy_size) > buf_size

    assert would_overflow, (
        f"[{label}] Test case should represent an overflow scenario"
    )

    # Simulate the check that MUST happen before any copy
    def validate_copy_bounds(buffer_size, dest_offset, size):
        """This check MUST exist in the implementation."""
        if dest_offset < 0 or size < 0:
            return False
        if dest_offset > buffer_size:
            return False
        if dest_offset + size > buffer_size:
            return False
        return True

    is_safe = validate_copy_bounds(buf_size, offset, copy_size)

    assert not is_safe, (
        f"[{label}] Bounds validation must reject: "
        f"offset({offset}) + size({copy_size}) = {offset + copy_size} "
        f"> buffer_size({buf_size})"
    )

    # Verify that a properly guarded implementation rejects the operation
    desc = NetworkDescriptor(buf_size, offset)
    frame_data = bytes([0xFF] * copy_size)

    with pytest.raises(ValueError):
        desc.safe_memcpy(frame_data)

    # Buffer integrity: must be completely unmodified
    assert bytes(desc.buffer) == bytes(buf_size), (
        f"[{label}] Buffer must remain pristine after rejected overflow attempt"
    )