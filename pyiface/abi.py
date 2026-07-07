"""abi.py -- the ONLY hardware mirror for /dev/carfield.

Every ioctl number, request-struct layout, cmd/status code, and errno
mapping lives here, each with a provenance comment naming the C header
and line it mirrors. Nothing hardware-shaped may appear in device.py or
demo.py (PYIFACE_SPEC.md R2; enforced informally by:
`grep -rn "0x" pyiface/ --include='*.py' | grep -v abi.py` should return
nothing).

Request structs are ctypes.Structure subclasses, not hand-packed
struct.Struct format strings -- none of the C request structs is
__attribute__((packed)), so natural C alignment inserts padding (most
sharply in CarfieldPagingTestReq, where 4 bytes sit between `lps` and
`header_phys`, not just at the end). ctypes reproduces that padding from
the field types alone; a hand-written format string would have to get it
exactly right by hand and silently corrupt the computed ioctl number if
it didn't (see the assertions below).
"""

import ctypes
import errno as _errno
import os as _os

# ---------------------------------------------------------------------------
# ioctl encoding (asm-generic/ioctl.h) -- computed from the same shifts the
# C _IOWR() macro uses, not a copied hex literal, so it can't drift from the
# macro expansion in carfield.h.
# ---------------------------------------------------------------------------
_IOC_NRBITS = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14

_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS

_IOC_READ = 2
_IOC_WRITE = 1


def _ioc(direction, type_, nr, size):
    return (
        (direction << _IOC_DIRSHIFT)
        | (type_ << _IOC_TYPESHIFT)
        | (nr << _IOC_NRSHIFT)
        | (size << _IOC_SIZESHIFT)
    )


def _iowr(type_, nr, ctypes_struct):
    return _ioc(_IOC_READ | _IOC_WRITE, type_, nr, ctypes.sizeof(ctypes_struct))


CARFIELD_MAGIC = ord("F")  # carfield.h:7


# ---------------------------------------------------------------------------
# Phase 0: CARFIELD_PING -- carfield.h:15-20
# ---------------------------------------------------------------------------
class CarfieldPing(ctypes.Structure):
    _fields_ = [
        ("value", ctypes.c_uint32),  # carfield.h:16
        ("echo", ctypes.c_uint32),  # carfield.h:17
    ]


assert ctypes.sizeof(CarfieldPing) == 8, (
    "CarfieldPing drifted from carfield.h struct carfield_ping -- header changed?"
)

CARFIELD_PING = _iowr(CARFIELD_MAGIC, 0, CarfieldPing)  # carfield.h:20


# ---------------------------------------------------------------------------
# Phase 2: CARFIELD_CLUSTER_RUN -- carfield.h:46-52
# ---------------------------------------------------------------------------
class CarfieldClusterRun(ctypes.Structure):
    _fields_ = [
        ("boot_addr", ctypes.c_uint32),  # carfield.h:47
        ("num_cores", ctypes.c_uint32),  # carfield.h:48
        ("result", ctypes.c_uint32),  # carfield.h:49
    ]


assert ctypes.sizeof(CarfieldClusterRun) == 12, (
    "CarfieldClusterRun drifted from carfield.h struct carfield_cluster_run -- header changed?"
)

CARFIELD_CLUSTER_RUN = _iowr(CARFIELD_MAGIC, 1, CarfieldClusterRun)  # carfield.h:52

# carfield.c:55 -- num_cores above this makes CARFIELD_CLUSTER_RUN return -EINVAL
INT_CLUSTER_NUM_CORES = 12


# ---------------------------------------------------------------------------
# Paging chain -- carfield_paging.h
# ---------------------------------------------------------------------------
CARFIELD_PAGING_MAGIC = 0xCA4F1E1D  # carfield_paging.h:56
CARFIELD_PAGING_HEADER_VERSION = 1  # carfield_paging.h:57

# PAGE_SIZE / sizeof(u32) -- carfield_paging.c:78. nop above this returns
# -E2BIG (CarfieldSizeError), not -EINVAL; see PYIFACE_SPEC.md §3/§7 and
# commit 2cca9f7 for why the driver was changed to distinguish the two.
CARFIELD_PAGING_MAP_MAX_ENTRIES = 4096 // 4


class CarfieldPagingTestReq(ctypes.Structure):
    _fields_ = [
        ("user_addr", ctypes.c_uint64),  # carfield_paging.h:125
        ("user_size", ctypes.c_uint64),  # carfield_paging.h:126
        ("dsz", ctypes.c_uint32),  # carfield_paging.h:129
        ("nop", ctypes.c_uint32),  # carfield_paging.h:130
        ("fpo", ctypes.c_uint32),  # carfield_paging.h:131
        ("fps", ctypes.c_uint32),  # carfield_paging.h:132
        ("lps", ctypes.c_uint32),  # carfield_paging.h:133
        # 4 bytes of C alignment padding land here (u64 needs 8-byte
        # alignment after five u32s) -- ctypes inserts this automatically,
        # it is not listed as a field.
        ("header_phys", ctypes.c_uint64),  # carfield_paging.h:134
        ("first_page_phys", ctypes.c_uint64),  # carfield_paging.h:135
        ("last_page_phys", ctypes.c_uint64),  # carfield_paging.h:136
    ]


assert ctypes.sizeof(CarfieldPagingTestReq) == 64, (
    "CarfieldPagingTestReq drifted from carfield_paging.h struct "
    "carfield_paging_test_req -- header changed, or ctypes padding "
    "assumption broke?"
)

CARFIELD_PAGING_TEST = _iowr(
    CARFIELD_MAGIC, 2, CarfieldPagingTestReq
)  # carfield_paging.h:139-140


# ---------------------------------------------------------------------------
# Mock OpenTitan consumer -- carfield_mock_ot.h. The ioctl itself is real
# and ratified; only the *consumer* behind it (the kthread) is a mock.
# ---------------------------------------------------------------------------

# carfield_mock_ot.h:20 -- driver-internal constant, hardcoded inside
# carfield.c's CARFIELD_MOCK_OT_XFORM handler (carfield.c:298). Never sent
# by userspace -- there is no `cmd` field on CarfieldMockOtReq below. See
# PYIFACE_SPEC.md R1 correction for why this isn't a submit(cmd, ...) API.
CARFIELD_MOCK_OT_CMD_XFORM = 0x0001

CARFIELD_MOCK_OT_OK = 0  # carfield_mock_ot.h:24
CARFIELD_MOCK_OT_ERR_MAGIC = 1  # carfield_mock_ot.h:25
CARFIELD_MOCK_OT_ERR_SIZE = 2  # carfield_mock_ot.h:26
CARFIELD_MOCK_OT_ERR_NOP = 3  # carfield_mock_ot.h:27 -- unreachable via the
# real ioctl path today, see carfield_mock_ot.c:49-58's own comment: the
# producer (carfield_paging_build) already rejects (-E2BIG) anything that
# would trigger this, before the mock ever sees it.
CARFIELD_MOCK_OT_ERR_GEOMETRY = 4  # carfield_mock_ot.h:28
CARFIELD_MOCK_OT_ERR_MAP = 5  # carfield_mock_ot.h:29
CARFIELD_MOCK_OT_ERR_MAP_ENTRY = 6  # carfield_mock_ot.h:30

# carfield_mock_ot.h:34 -- sentinel meaning "no reply happened" (mock_no_reply,
# or a signal). Reused Python-side to disambiguate CARFIELD_MOCK_OT_XFORM's
# two different -EFAULT causes -- see device.py's _map_error().
CARFIELD_MOCK_OT_STATUS_NONE = 0xFFFFFFFF


class CarfieldMockOtReq(ctypes.Structure):
    _fields_ = [
        ("user_addr", ctypes.c_uint64),  # carfield_mock_ot.h:79
        ("user_size", ctypes.c_uint64),  # carfield_mock_ot.h:80
        ("mock_status", ctypes.c_uint32),  # carfield_mock_ot.h:82
        # 4 trailing pad bytes (20 -> 24), largest member is u64.
    ]


assert ctypes.sizeof(CarfieldMockOtReq) == 24, (
    "CarfieldMockOtReq drifted from carfield_mock_ot.h struct "
    "carfield_mock_ot_req -- header changed, or ctypes padding assumption broke?"
)

CARFIELD_MOCK_OT_XFORM = _iowr(
    CARFIELD_MAGIC, 3, CarfieldMockOtReq
)  # carfield_mock_ot.h:85-86


# ---------------------------------------------------------------------------
# Exceptions -- every raised error names the op that produced it, because
# errno alone is ambiguous (PYIFACE_SPEC.md §3): EFAULT and ENXIO are each
# reused by the driver for two unrelated failures.
# ---------------------------------------------------------------------------
class CarfieldError(Exception):
    """Base class for every mapped /dev/carfield failure. Always carries
    the op name and the raw errno."""

    def __init__(self, op, errnum, message=None):
        self.op = op
        self.errno = errnum
        msg = message or _os.strerror(errnum)
        super().__init__(f"{op}: {msg} (errno={errnum})")


class CarfieldTransportError(CarfieldError):
    """copy_from_user/copy_to_user failed -- the request never reached the
    op's own logic at all. Distinct from any op's own rejection codes,
    even when the raw errno (EFAULT) is the same one an op-level rejection
    would also use."""


class CarfieldBadRequest(CarfieldError):
    pass


class CarfieldSizeError(CarfieldError):
    pass


class CarfieldAddressRange(CarfieldError):
    pass


class CarfieldNotAvailable(CarfieldError):
    pass


class CarfieldTimeout(CarfieldError):
    pass


class CarfieldBadHeader(CarfieldError):
    pass


class CarfieldGeometryError(CarfieldError):
    pass


class CarfieldMockError(CarfieldError):
    pass


class CarfieldNoHardware(CarfieldError):
    pass


# ---------------------------------------------------------------------------
# Per-op errno -> exception tables (PYIFACE_SPEC.md §3). Deliberately not
# one global table: the same errno means different things depending on
# which ioctl produced it.
# ---------------------------------------------------------------------------
_PAGING_TEST_ERRNOS = {
    _errno.EINVAL: CarfieldBadRequest,  # carfield_paging.c:65-66 (zero-size / overflow)
    _errno.E2BIG: CarfieldSizeError,  # carfield_paging.c:78-79 (nop > 1024)
    _errno.ERANGE: CarfieldAddressRange,  # carfield_paging.c:117-121 (data page phys > 4GB)
}

_XFORM_ERRNOS = {
    _errno.EINVAL: CarfieldBadRequest,
    _errno.E2BIG: CarfieldSizeError,
    _errno.ERANGE: CarfieldAddressRange,
    _errno.ENODEV: CarfieldNotAvailable,  # carfield.c:288, mock_ot=0
    _errno.ETIMEDOUT: CarfieldTimeout,  # carfield_mock_ot.c:113, mock_no_reply / wedged
    _errno.EILSEQ: CarfieldBadHeader,  # ERR_MAGIC, carfield_mock_ot.c:128
    _errno.EBADMSG: CarfieldGeometryError,  # ERR_GEOMETRY, carfield_mock_ot.c:131
    # ERR_MAP also maps to EFAULT (carfield_mock_ot.c:132), same errno as a
    # copy_from_user/copy_to_user failure on this exact ioctl. This default
    # assumes the ERR_MAP case (status was actually populated); device.py's
    # _map_error() overrides to CarfieldTransportError when the sentinel
    # check shows the request never reached the mock at all.
    _errno.EFAULT: CarfieldBadHeader,
    _errno.ENXIO: CarfieldBadHeader,  # ERR_MAP_ENTRY, carfield_mock_ot.c:133
    _errno.EIO: CarfieldMockError,  # mock_force_err / unknown status, carfield_mock_ot.c:134
}

_CLUSTER_RUN_ERRNOS = {
    _errno.ENXIO: CarfieldNoHardware,  # carfield.c:204-206, soc_ctrl/int_cluster not mapped
    _errno.EINVAL: CarfieldBadRequest,  # carfield.c:212-216, num_cores too large
    _errno.ETIMEDOUT: CarfieldTimeout,  # carfield.c:238-241, EOC wait timeout
}

ERRNO_TABLES = {
    "paging_test": _PAGING_TEST_ERRNOS,
    "cluster_run": _CLUSTER_RUN_ERRNOS,
    "xform": _XFORM_ERRNOS,
}


def exception_for(op, errnum):
    """Map (op, errno) to the right CarfieldError subclass using the
    per-op table above. Callers with op/errno combos that need extra
    context beyond the errno (currently just xform's EFAULT, see
    device.py) should resolve that ambiguity before calling this."""
    table = ERRNO_TABLES.get(op, {})
    exc_cls = table.get(errnum, CarfieldError)
    return exc_cls(op, errnum)
