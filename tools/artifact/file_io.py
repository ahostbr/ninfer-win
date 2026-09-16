"""Keep large offline transfers from retaining whole artifacts in Linux's page cache."""

from __future__ import annotations

import mmap
import os

IO_CHUNK_BYTES = 8 * 1024 * 1024
WRITEBACK_BYTES = 64 * 1024 * 1024
# sysconf is POSIX-only; mmap.PAGESIZE reports the same value and exists everywhere.
_PAGE_BYTES = mmap.PAGESIZE

# posix_fadvise has no Windows equivalent. Dropping the hint costs page-cache residency, not
# correctness — the writes and their ordering are unaffected — so the call becomes a no-op
# rather than a platform branch at every call site.
_HAS_FADVISE = hasattr(os, "posix_fadvise")
# Windows has no fdatasync. fsync is a superset: it also flushes metadata.
_sync = getattr(os, "fdatasync", os.fsync)


# pread/pwrite are POSIX. Windows has no positional read or write, so the offset is applied
# with an explicit seek. That is NOT pread's atomicity: a seek plus a read is two operations
# against one shared file offset. These tools are single-threaded and use one descriptor per
# file, which is the condition that makes the substitution sound.
if hasattr(os, "pread"):

    def pread(fd: int, count: int, offset: int) -> bytes:
        return os.pread(fd, count, offset)

    def pwrite(fd: int, data, offset: int) -> int:
        return os.pwrite(fd, data, offset)

else:

    def pread(fd: int, count: int, offset: int) -> bytes:
        os.lseek(fd, offset, os.SEEK_SET)
        return os.read(fd, count)

    def pwrite(fd: int, data, offset: int) -> int:
        os.lseek(fd, offset, os.SEEK_SET)
        return os.write(fd, data)


def discard_cached_pages(fd: int, offset: int = 0, count: int | None = None) -> None:
    if not _HAS_FADVISE:
        return
    if count is None:
        os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
    elif count > 0:
        begin = offset // _PAGE_BYTES * _PAGE_BYTES
        end = (offset + count + _PAGE_BYTES - 1) // _PAGE_BYTES * _PAGE_BYTES
        os.posix_fadvise(fd, begin, end - begin, os.POSIX_FADV_DONTNEED)


class Writeback:
    """Bound dirty output across all open shards; release clean pages after writeback."""

    def __init__(self) -> None:
        self._bytes = 0
        self._fds: set[int] = set()

    def written(self, fd: int, count: int) -> None:
        self._fds.add(fd)
        self._bytes += count
        if self._bytes >= WRITEBACK_BYTES:
            self.flush()

    def flush(self) -> None:
        for fd in self._fds:
            _sync(fd)
            discard_cached_pages(fd)
        self._fds.clear()
        self._bytes = 0
