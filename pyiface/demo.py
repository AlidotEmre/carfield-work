"""demo.py -- MOCK-ONLY conveniences. Disposable.

xform() exists to drive MOCK_OT_SPEC.md §7 from Python. It is NOT a
preview of the real API surface: the real decrypt flow's output
destination is still unresolved -- one of the open contradictions in
Daniele's most recent mail (see project_alsaqr.md's "Daniele'den Yeni
Mail" section and PYIFACE_SPEC.md §6) is whether OT writes back into the
host's pinned buffer at all, or into PULP L2 instead. Either way, a real
crypto op will not be a same-buffer XOR echo the way this mock is (see
TITANSSL_ANALYSIS.md §5 RESERVE on input/output size asymmetry). Nothing
in this module should be mirrored into device.py's ratified surface when
real ops arrive -- they get their own ioctl, struct, and method there.
"""

from . import abi


def xform(dev, data: bytes) -> bytes:
    """MOCK-ONLY. Sends `data` through CARFIELD_MOCK_OT_XFORM (requires
    the module loaded with mock_ot=1) and returns the transformed bytes
    (XOR 0x5A in place on the real, hardware-backed pages -- see
    MOCK_OT_SPEC.md §4). Raises a CarfieldError subclass on any §5
    rejection, ENODEV if mock_ot=0, or CarfieldTimeout on mock_no_reply.
    """
    size = len(data)
    buf, addr = dev.alloc(size)
    buf[0:size] = data
    dev._paging_op(
        "xform",
        abi.CARFIELD_MOCK_OT_XFORM,
        abi.CarfieldMockOtReq,
        addr,
        size,
        # Pre-fill with the "no reply yet" sentinel so device.py can tell
        # a copy-stage -EFAULT from an ERR_MAP -EFAULT after the fact --
        # see CarfieldDevice._map_error().
        mock_status=abi.CARFIELD_MOCK_OT_STATUS_NONE,
    )
    return bytes(buf[0:size])
