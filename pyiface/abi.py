"""abi.py -- the ONLY hardware mirror for /dev/alsaqr.

Every ioctl number, request-struct layout, cmd/status code, and errno
mapping lives here, each with a provenance comment naming the C header
and line it mirrors. Nothing hardware-shaped may appear in device.py or
demo.py (PYIFACE_SPEC.md R2; enforced informally by:
`grep -rn "0x" pyiface/ --include='*.py' | grep -v abi.py` should return
nothing).

Request structs are ctypes.Structure subclasses, not hand-packed
struct.Struct format strings -- none of the C request structs is
__attribute__((packed)), so natural C alignment inserts padding (most
sharply in AlsaqrPagingTestReq, where 4 bytes sit between `lps` and
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
# macro expansion in alsaqr.h.
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


ALSAQR_MAGIC = ord("A")  # alsaqr.h:7


# ---------------------------------------------------------------------------
# Phase 0: ALSAQR_PING -- alsaqr.h:15-20
# ---------------------------------------------------------------------------
class AlsaqrPing(ctypes.Structure):
    _fields_ = [
        ("value", ctypes.c_uint32),  # alsaqr.h:16
        ("echo", ctypes.c_uint32),  # alsaqr.h:17
    ]


assert ctypes.sizeof(AlsaqrPing) == 8, (
    "AlsaqrPing drifted from alsaqr.h struct alsaqr_ping -- header changed?"
)

ALSAQR_PING = _iowr(ALSAQR_MAGIC, 0, AlsaqrPing)  # alsaqr.h:20


# ---------------------------------------------------------------------------
# Paging chain -- alsaqr_paging.h
# ---------------------------------------------------------------------------
ALSAQR_PAGING_MAGIC = 0xCA4F1E1D  # alsaqr_paging.h:56
ALSAQR_PAGING_HEADER_VERSION = 1  # alsaqr_paging.h:57

# PAGE_SIZE / sizeof(u32) -- alsaqr_paging.c:78. nop above this returns
# -E2BIG (AlsaqrSizeError), not -EINVAL; see PYIFACE_SPEC.md §3/§7 and
# commit 2cca9f7 for why the driver was changed to distinguish the two.
ALSAQR_PAGING_MAP_MAX_ENTRIES = 4096 // 4


class AlsaqrPagingTestReq(ctypes.Structure):
    _fields_ = [
        ("user_addr", ctypes.c_uint64),  # alsaqr_paging.h:125
        ("user_size", ctypes.c_uint64),  # alsaqr_paging.h:126
        ("dsz", ctypes.c_uint32),  # alsaqr_paging.h:129
        ("nop", ctypes.c_uint32),  # alsaqr_paging.h:130
        ("fpo", ctypes.c_uint32),  # alsaqr_paging.h:131
        ("fps", ctypes.c_uint32),  # alsaqr_paging.h:132
        ("lps", ctypes.c_uint32),  # alsaqr_paging.h:133
        # 4 bytes of C alignment padding land here (u64 needs 8-byte
        # alignment after five u32s) -- ctypes inserts this automatically,
        # it is not listed as a field.
        ("header_phys", ctypes.c_uint64),  # alsaqr_paging.h:134
        ("first_page_phys", ctypes.c_uint64),  # alsaqr_paging.h:135
        ("last_page_phys", ctypes.c_uint64),  # alsaqr_paging.h:136
    ]


assert ctypes.sizeof(AlsaqrPagingTestReq) == 64, (
    "AlsaqrPagingTestReq drifted from alsaqr_paging.h struct "
    "alsaqr_paging_test_req -- header changed, or ctypes padding "
    "assumption broke?"
)

ALSAQR_PAGING_TEST = _iowr(
    ALSAQR_MAGIC, 2, AlsaqrPagingTestReq
)  # alsaqr_paging.h:139-140


# ---------------------------------------------------------------------------
# Mock OpenTitan consumer -- alsaqr_mock_ot.h. The ioctl itself is real
# and ratified; only the *consumer* behind it (the kthread) is a mock.
# ---------------------------------------------------------------------------

# alsaqr_mock_ot.h:20 -- driver-internal constant, hardcoded inside
# alsaqr.c's ALSAQR_OT_XFORM handler (alsaqr.c:298). Never sent
# by userspace -- there is no `cmd` field on AlsaqrOtXformReq below. See
# PYIFACE_SPEC.md R1 correction for why this isn't a submit(cmd, ...) API.
ALSAQR_OT_CMD_XFORM = 0x0001

ALSAQR_MOCK_OT_OK = 0  # alsaqr_mock_ot.h:24
ALSAQR_MOCK_OT_ERR_MAGIC = 1  # alsaqr_mock_ot.h:25
ALSAQR_MOCK_OT_ERR_SIZE = 2  # alsaqr_mock_ot.h:26
ALSAQR_MOCK_OT_ERR_NOP = 3  # alsaqr_mock_ot.h:27 -- unreachable via the
# real ioctl path today, see alsaqr_mock_ot.c:49-58's own comment: the
# producer (alsaqr_paging_build) already rejects (-E2BIG) anything that
# would trigger this, before the mock ever sees it.
ALSAQR_MOCK_OT_ERR_GEOMETRY = 4  # alsaqr_mock_ot.h:28
ALSAQR_MOCK_OT_ERR_MAP = 5  # alsaqr_mock_ot.h:29
ALSAQR_MOCK_OT_ERR_MAP_ENTRY = 6  # alsaqr_mock_ot.h:30

# alsaqr_mock_ot.h:34 -- sentinel meaning "no reply happened" (mock_no_reply,
# or a signal). Reused Python-side to disambiguate ALSAQR_OT_XFORM's
# two different -EFAULT causes -- see device.py's _map_error().
ALSAQR_OT_STATUS_NONE = 0xFFFFFFFF


class AlsaqrOtXformReq(ctypes.Structure):
    _fields_ = [
        ("user_addr", ctypes.c_uint64),  # alsaqr_mock_ot.h:79
        ("user_size", ctypes.c_uint64),  # alsaqr_mock_ot.h:80
        ("ot_status", ctypes.c_uint32),  # alsaqr_mock_ot.h:82
        # 4 trailing pad bytes (20 -> 24), largest member is u64.
    ]


assert ctypes.sizeof(AlsaqrOtXformReq) == 24, (
    "AlsaqrOtXformReq drifted from alsaqr_mock_ot.h struct "
    "alsaqr_ot_xform_req -- header changed, or ctypes padding assumption broke?"
)

ALSAQR_OT_XFORM = _iowr(
    ALSAQR_MAGIC, 3, AlsaqrOtXformReq
)  # alsaqr_mock_ot.h:85-86


# ---------------------------------------------------------------------------
# Exceptions -- every raised error names the op that produced it, because
# errno alone is ambiguous (PYIFACE_SPEC.md §3): EFAULT and ENXIO are each
# reused by the driver for two unrelated failures.
# ---------------------------------------------------------------------------
class AlsaqrError(Exception):
    """Base class for every mapped /dev/alsaqr failure. Always carries
    the op name and the raw errno."""

    def __init__(self, op, errnum, message=None):
        self.op = op
        self.errno = errnum
        msg = message or _os.strerror(errnum)
        super().__init__(f"{op}: {msg} (errno={errnum})")


class AlsaqrTransportError(AlsaqrError):
    """copy_from_user/copy_to_user failed -- the request never reached the
    op's own logic at all. Distinct from any op's own rejection codes,
    even when the raw errno (EFAULT) is the same one an op-level rejection
    would also use."""


class AlsaqrBadRequest(AlsaqrError):
    pass


class AlsaqrSizeError(AlsaqrError):
    pass


class AlsaqrAddressRange(AlsaqrError):
    pass


class AlsaqrNotAvailable(AlsaqrError):
    pass


class AlsaqrTimeout(AlsaqrError):
    pass


class AlsaqrBadHeader(AlsaqrError):
    pass


class AlsaqrGeometryError(AlsaqrError):
    pass


class AlsaqrMockError(AlsaqrError):
    pass


# ---------------------------------------------------------------------------
# Per-op errno -> exception tables (PYIFACE_SPEC.md §3). Deliberately not
# one global table: the same errno means different things depending on
# which ioctl produced it.
# ---------------------------------------------------------------------------
_PAGING_TEST_ERRNOS = {
    _errno.EINVAL: AlsaqrBadRequest,  # alsaqr_paging.c:65-66 (zero-size / overflow)
    _errno.E2BIG: AlsaqrSizeError,  # alsaqr_paging.c:78-79 (nop > 1024)
    _errno.ERANGE: AlsaqrAddressRange,  # alsaqr_paging.c:117-121 (data page phys > 4GB)
}

_XFORM_ERRNOS = {
    _errno.EINVAL: AlsaqrBadRequest,
    _errno.E2BIG: AlsaqrSizeError,
    _errno.ERANGE: AlsaqrAddressRange,
    _errno.ENODEV: AlsaqrNotAvailable,  # alsaqr.c:288, mock_ot=0
    _errno.ETIMEDOUT: AlsaqrTimeout,  # alsaqr_mock_ot.c:113, mock_no_reply / wedged
    _errno.EILSEQ: AlsaqrBadHeader,  # ERR_MAGIC, alsaqr_mock_ot.c:128
    _errno.EBADMSG: AlsaqrGeometryError,  # ERR_GEOMETRY, alsaqr_mock_ot.c:131
    # ERR_MAP also maps to EFAULT (alsaqr_mock_ot.c:132), same errno as a
    # copy_from_user/copy_to_user failure on this exact ioctl. This default
    # assumes the ERR_MAP case (status was actually populated); device.py's
    # _map_error() overrides to AlsaqrTransportError when the sentinel
    # check shows the request never reached the mock at all.
    _errno.EFAULT: AlsaqrBadHeader,
    _errno.ENXIO: AlsaqrBadHeader,  # ERR_MAP_ENTRY, alsaqr_mock_ot.c:133
    _errno.EIO: AlsaqrMockError,  # mock_force_err / unknown status, alsaqr_mock_ot.c:134
}

ERRNO_TABLES = {
    "paging_test": _PAGING_TEST_ERRNOS,
    "xform": _XFORM_ERRNOS,
}


def exception_for(op, errnum):
    """Map (op, errno) to the right AlsaqrError subclass using the
    per-op table above. Callers with op/errno combos that need extra
    context beyond the errno (currently just xform's EFAULT, see
    device.py) should resolve that ambiguity before calling this."""
    table = ERRNO_TABLES.get(op, {})
    exc_cls = table.get(errnum, AlsaqrError)
    return exc_cls(op, errnum)
