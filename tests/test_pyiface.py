"""End-to-end Python test suite for /dev/carfield, mirroring
MOCK_OT_SPEC.md §7 (and tests/mock_ot_test.c) from userspace Python, plus
the Python-specific cases in PYIFACE_SPEC.md §7.

Requires the module loaded with mock_ot=1:

    sudo insmod carfield-mod.ko mock_ot=1
    sudo pytest tests/test_pyiface.py

Fault-injection params are toggled at runtime through
/sys/module/carfield_mod/parameters/<name> (all 0644 except mock_ot
itself, which is load-time only -- see PYIFACE_SPEC.md §7).

Not covered here (manual step, same as mock_ot_test.c): after the suite
passes, `rmmod carfield-mod` and check `sudo dmesg` for leak/Bad-page
warnings. A test process holding /dev/carfield open can't meaningfully
rmmod itself.
"""

import gc
import os

import pytest

from pyiface import abi, demo
from pyiface.device import CarfieldDevice

XOR_KEY = 0x5A
PAGE_SIZE = 4096
_PARAM_DIR = "/sys/module/carfield_mod/parameters"


def _set_param(name, value):
    with open(os.path.join(_PARAM_DIR, name), "w") as f:
        f.write(str(value))


def _fill_pattern(n):
    return bytes((i * 167 + 7) & 0xFF for i in range(n))


@pytest.fixture(scope="module")
def dev():
    try:
        d = CarfieldDevice()
    except OSError as e:
        pytest.skip(f"/dev/carfield not available: {e}")
        return
    yield d
    d.close()


def _run_and_check(dev, total, off, size, key=XOR_KEY):
    """Fill `total` bytes with a known pattern, xform() the [off, off+size)
    slice, and verify every payload byte flipped and every guard byte
    (everything else in [0, total)) is untouched -- mirrors
    tests/mock_ot_test.c's run_and_check()."""
    shadow = _fill_pattern(total)
    buf, addr = dev.alloc(total)
    buf[0:total] = shadow

    dev._paging_op(
        "xform", abi.CARFIELD_MOCK_OT_XFORM, abi.CarfieldMockOtReq,
        addr + off, size, mock_status=abi.CARFIELD_MOCK_OT_STATUS_NONE,
    )

    got = bytes(buf[0:total])
    for i in range(total):
        in_payload = off <= i < off + size
        expect = (shadow[i] ^ key) if in_payload else shadow[i]
        assert got[i] == expect, (
            f"byte {i} ({'payload' if in_payload else 'guard'}): "
            f"got 0x{got[i]:02x} expected 0x{expect:02x}"
        )


GEOMETRY_CASES = [
    pytest.param(PAGE_SIZE, 64, 200, id="single-page"),
    pytest.param(4 * PAGE_SIZE, 0x123, 2 * PAGE_SIZE + 100, id="page-straddling"),
    pytest.param(20 * PAGE_SIZE, 17, 20 * PAGE_SIZE - 17 - 23, id="large-scattered"),
    pytest.param(3 * PAGE_SIZE, 0, 3 * PAGE_SIZE, id="mmap-aligned-fpo0"),
]


@pytest.mark.parametrize("total, off, size", GEOMETRY_CASES)
def test_geometry_suite_three_times(dev, total, off, size):
    for _ in range(3):
        _run_and_check(dev, total, off, size)
        gc.collect()  # buffer-lifetime safety, per PYIFACE_SPEC.md §7


def test_demo_xform_roundtrip(dev):
    """Exercises the public demo.xform() convenience directly, not just
    the internal _paging_op() plumbing the geometry suite above uses."""
    data = _fill_pattern(300)
    out = demo.xform(dev, data)
    assert out == bytes(b ^ XOR_KEY for b in data)


def test_mock_no_reply_times_out(dev):
    _set_param("mock_no_reply", "1")
    try:
        with pytest.raises(abi.CarfieldTimeout):
            demo.xform(dev, _fill_pattern(64))
    finally:
        _set_param("mock_no_reply", "0")


def test_mock_corrupt_magic_rejected(dev):
    """The one §5 rejection case reachable through the normal ioctl path
    (see carfield_mock_ot.c's comment on this test-only param)."""
    _set_param("mock_corrupt_magic", "1")
    try:
        with pytest.raises(abi.CarfieldBadHeader):
            demo.xform(dev, _fill_pattern(64))
    finally:
        _set_param("mock_corrupt_magic", "0")


def test_mock_bad_xform_fails_verification(dev):
    """mock_bad_xform=1 must make the *correct-key* comparison fail --
    proves this suite isn't vacuous (MOCK_OT_SPEC.md §6)."""
    _set_param("mock_bad_xform", "1")
    try:
        data = _fill_pattern(128)
        out = demo.xform(dev, data)
        correct = bytes(b ^ XOR_KEY for b in data)
        assert out != correct
    finally:
        _set_param("mock_bad_xform", "0")


def test_zero_size_is_bad_request(dev):
    """size==0 -> -EINVAL -> CarfieldBadRequest (carfield_paging.c:65-66)."""
    _buf, addr = dev.alloc(PAGE_SIZE)
    with pytest.raises(abi.CarfieldBadRequest):
        dev.paging_test(addr, 0)


def test_oversize_is_size_error(dev):
    """nop > CARFIELD_PAGING_MAP_MAX_ENTRIES (1024) at fpo=0 -> -E2BIG ->
    CarfieldSizeError (carfield_paging.c:78-79, fixed in commit 2cca9f7 to
    return -E2BIG instead of -EINVAL specifically so this is distinct from
    test_zero_size_is_bad_request above). This never reaches the mock's
    own ERR_NOP path -- see PYIFACE_SPEC.md §7."""
    size = (abi.CARFIELD_PAGING_MAP_MAX_ENTRIES + 1) * PAGE_SIZE
    _buf, addr = dev.alloc(size)
    with pytest.raises(abi.CarfieldSizeError):
        dev.paging_test(addr, size)
