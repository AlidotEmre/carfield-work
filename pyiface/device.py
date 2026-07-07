"""device.py -- CarfieldDevice: the public Python surface for /dev/carfield.

Ratified-only surface (PYIFACE_SPEC.md R1): one method per existing ioctl
(ping, cluster_run, paging_test). No generic submit(cmd, ...) -- none of
the C request structs carries a cmd field, so there is nothing for such a
parameter to bind to; see PYIFACE_SPEC.md's R1 correction. Mock-only
conveniences (xform) live in demo.py, built on the same _paging_op()
helper this module exposes for exactly that reason.
"""

import ctypes
import errno
import fcntl
import mmap
import os

from . import abi

_DEV_PATH = "/dev/carfield"


class CarfieldDevice:
    def __init__(self, path=_DEV_PATH):
        try:
            self._fd = os.open(path, os.O_RDWR)
        except OSError as e:
            raise OSError(
                f"{path}: {e.strerror} -- is the carfield-mod module loaded? "
                f"(sudo insmod carfield-mod.ko [mock_ot=1])"
            ) from e

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def close(self):
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None

    # -- buffer helpers -------------------------------------------------

    def alloc(self, size, offset=0):
        """Anonymous, page-aligned buffer of at least offset+size bytes.
        Returns (mmap_object, addr) where addr is the address of the
        offset-th byte -- pass offset>0 to exercise the fpo!=0 path.

        GC hazard: the caller must keep the returned mmap object alive
        for as long as `addr` is used (e.g. across an ioctl call). If it
        is dropped, the anonymous mapping can be torn down and `addr`
        becomes invalid or gets reused by something else. CarfieldDevice
        does not retain a reference on the caller's behalf -- that would
        mask exactly the hazard the test suite's gc.collect() cases exist
        to catch.
        """
        total = offset + size
        buf = mmap.mmap(-1, total)
        addr = ctypes.addressof(ctypes.c_char.from_buffer(buf)) + offset
        return buf, addr

    # -- shared ioctl plumbing -------------------------------------------

    def _ioctl(self, op, request_code, req):
        try:
            fcntl.ioctl(self._fd, request_code, req)
        except OSError as e:
            raise self._map_error(op, e.errno, req) from None
        return req

    def _map_error(self, op, errnum, req):
        """Most (op, errno) pairs map cleanly via abi.exception_for(). The
        one exception: CARFIELD_MOCK_OT_XFORM's -EFAULT is ambiguous by
        construction (carfield_mock_ot.c's ERR_MAP maps to the same errno
        copy_from_user/copy_to_user use) -- see PYIFACE_SPEC.md §3. The
        driver mutates the ioctl buffer in place whenever copy_to_user
        succeeded, regardless of the ioctl's return value, so checking
        whether mock_status still holds the sentinel we pre-filled it
        with tells us which -EFAULT this was.
        """
        if op == "xform" and errnum == errno.EFAULT:
            if getattr(req, "mock_status", None) == abi.CARFIELD_MOCK_OT_STATUS_NONE:
                return abi.CarfieldTransportError(
                    op,
                    errnum,
                    "copy_from_user/copy_to_user failed before the mock ever saw the request",
                )
        return abi.exception_for(op, errnum)

    def _paging_op(self, op, request_code, req_type, addr, size, **extra_fields):
        """Shared shape behind paging_test() and demo.xform() (and, later,
        any real op that also pins [addr, addr+size) through the paging
        chain): build the request struct, run the ioctl, return it filled
        in on success or raise the mapped exception on failure.
        """
        req = req_type(user_addr=addr, user_size=size, **extra_fields)
        return self._ioctl(op, request_code, req)

    # -- ratified ops -----------------------------------------------------

    def ping(self, value):
        """CARFIELD_PING: sanity-check the driver-userspace channel, no
        hardware required. Returns the echoed value."""
        req = abi.CarfieldPing(value=value, echo=0)
        self._ioctl("ping", abi.CARFIELD_PING, req)
        return req.echo

    def cluster_run(self, boot_addr, num_cores=0):
        """CARFIELD_CLUSTER_RUN: boot the PULP cluster at boot_addr and
        wait for EOC. num_cores=0 means all INT_CLUSTER_NUM_CORES cores.
        Returns the cluster's result register."""
        req = abi.CarfieldClusterRun(
            boot_addr=boot_addr, num_cores=num_cores, result=0
        )
        self._ioctl("cluster_run", abi.CARFIELD_CLUSTER_RUN, req)
        return req.result

    def paging_test(self, addr, size):
        """CARFIELD_PAGING_TEST: debug/test-only. Exercises the pin/build/
        release chain over [addr, addr+size) without any consumer, and
        returns the computed geometry (dsz/nop/fpo/fps/lps) and the
        header/first-page/last-page physical addresses."""
        return self._paging_op(
            "paging_test", abi.CARFIELD_PAGING_TEST, abi.CarfieldPagingTestReq,
            addr, size,
        )
