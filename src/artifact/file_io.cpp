#include "artifact/file_io.h"

#include "artifact/framing.h"
#include "artifact/schema.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>

#if defined(_WIN32)
#include <system_error>

#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ninfer::artifact {
namespace {

#if defined(_WIN32)

// The largest byte count a single ReadFile can express.
constexpr std::size_t kMaximumSingleRead = static_cast<std::size_t>(
    (std::numeric_limits<DWORD>::max)());

[[noreturn]] void fail(const std::filesystem::path& path, const char* operation, DWORD error) {
    throw ArtifactError(path.string() + ": " + operation + ": " +
                        std::system_category().message(static_cast<int>(error)));
}

[[noreturn]] void fail(const std::filesystem::path& path, const char* operation) {
    fail(path, operation, ::GetLastError());
}

HANDLE handle_of(void* value) noexcept { return static_cast<HANDLE>(value); }

#else

[[noreturn]] void fail(const std::filesystem::path& path, const char* operation) {
    throw ArtifactError(path.string() + ": " + operation + ": " + std::strerror(errno));
}

off_t file_offset(std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        throw ArtifactError("file offset exceeds positional I/O range");
    }
    return static_cast<off_t>(offset);
}

#endif

} // namespace

#if defined(_WIN32)

// Both reads below pass the offset through OVERLAPPED on a synchronous handle, which mirrors
// pread for a single reader but not pread's thread safety: a synchronous handle also advances
// its own file pointer, so two concurrent reads on one InputFile would race. Every caller today
// reads from one thread (Reader, and the sequential span loop in materializer.cpp).
// ponytail: single-reader positional I/O; open with FILE_FLAG_OVERLAPPED and a per-read event
// if artifact loading is ever parallelized.

InputFile::InputFile(std::filesystem::path path) : path_(std::move(path)) {
    // std::filesystem::path stores wchar_t on Windows, so the wide entry point takes it
    // directly and the artifact path keeps whatever characters the user's filesystem holds.
    const HANDLE file = ::CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { fail(path_, "open"); }
    fd_ = file;

    if (::GetFileType(file) != FILE_TYPE_DISK) {
        ::CloseHandle(file);
        fd_ = nullptr;
        throw ArtifactError(path_.string() + ": expected a regular file");
    }

    LARGE_INTEGER size{};
    if (::GetFileSizeEx(file, &size) == 0) {
        const auto error = ::GetLastError();
        ::CloseHandle(file);
        fd_ = nullptr;
        fail(path_, "stat", error);
    }
    // QuadPart is signed; a negative length is not representable for a disk file, but the
    // comparison is kept so the unsigned cast below can never wrap.
    if (size.QuadPart < 0) {
        ::CloseHandle(file);
        fd_ = nullptr;
        throw ArtifactError(path_.string() + ": expected a regular file");
    }
    bytes_ = static_cast<std::uint64_t>(size.QuadPart);
}

InputFile::~InputFile() {
    if (direct_fd_ != nullptr) { ::CloseHandle(handle_of(direct_fd_)); }
    if (fd_ != nullptr) { ::CloseHandle(handle_of(fd_)); }
}

void InputFile::read_exact(std::uint64_t offset, std::span<std::byte> destination) const {
    if (offset > bytes_ || destination.size() > bytes_ - offset) {
        throw ArtifactError(path_.string() + ": read exceeds file length");
    }
    while (!destination.empty()) {
        const auto count =
            static_cast<DWORD>(std::min<std::size_t>(destination.size(), 64ULL * 1024 * 1024));
        bool end_of_file = false;
        DWORD read       = 0;
        {
            OVERLAPPED overlapped{};
            overlapped.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFULL);
            overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32U);
            if (::ReadFile(handle_of(fd_), destination.data(), count, &read, &overlapped) == 0) {
                const auto error = ::GetLastError();
                if (error != ERROR_HANDLE_EOF) { fail(path_, "read", error); }
                end_of_file = true;
            }
        }
        if (end_of_file || read == 0) { throw ArtifactError(path_.string() + ": unexpected EOF"); }
        offset += read;
        destination = destination.subspan(read);
    }
}

std::size_t InputFile::read_direct(std::uint64_t offset, std::span<std::byte> destination) const {
    if (offset % kPayloadAlignment || destination.size() % kPayloadAlignment ||
        reinterpret_cast<std::uintptr_t>(destination.data()) % kPayloadAlignment ||
        destination.size() > kMaximumSingleRead) {
        throw ArtifactError(path_.string() + ": unaligned or oversized direct read");
    }
    if (destination.empty()) { return 0; }
    if (direct_fd_ == nullptr) {
        // FILE_FLAG_NO_BUFFERING is the O_DIRECT equivalent: it bypasses the system cache and
        // requires the offset, the byte count and the buffer address to be multiples of the
        // volume sector size. kPayloadAlignment is 4096, which covers every sector size in
        // use, and the caller's staging buffers are page-aligned, so the guard above is also
        // the check this flag needs.
        const HANDLE file =
            ::CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
        if (file == INVALID_HANDLE_VALUE) { fail(path_, "open direct"); }
        direct_fd_ = file;
    }
    OVERLAPPED overlapped{};
    overlapped.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFULL);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32U);
    DWORD read            = 0;
    if (::ReadFile(handle_of(direct_fd_), destination.data(),
                   static_cast<DWORD>(destination.size()), &read, &overlapped) == 0) {
        const auto error = ::GetLastError();
        // A final block that starts inside the file but extends past its end is a short read
        // on POSIX; unbuffered Windows reads report it as handle-EOF with the bytes already
        // delivered, so both platforms return the same short count.
        if (error != ERROR_HANDLE_EOF) { fail(path_, "direct read", error); }
    }
    return read;
}

#else

InputFile::InputFile(std::filesystem::path path) : path_(std::move(path)) {
    fd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) { fail(path_, "open"); }

    struct stat status {};

    if (::fstat(fd_, &status) != 0) {
        const auto error = errno;
        ::close(fd_);
        fd_   = -1;
        errno = error;
        fail(path_, "fstat");
    }
    if (status.st_size < 0 || !S_ISREG(status.st_mode)) {
        ::close(fd_);
        fd_ = -1;
        throw ArtifactError(path_.string() + ": expected a regular file");
    }
    bytes_ = static_cast<std::uint64_t>(status.st_size);
}

InputFile::~InputFile() {
    if (direct_fd_ >= 0) { ::close(direct_fd_); }
    if (fd_ >= 0) { ::close(fd_); }
}

void InputFile::read_exact(std::uint64_t offset, std::span<std::byte> destination) const {
    if (offset > bytes_ || destination.size() > bytes_ - offset) {
        throw ArtifactError(path_.string() + ": read exceeds file length");
    }
    while (!destination.empty()) {
        const auto count = std::min<std::size_t>(destination.size(), 64ULL * 1024 * 1024);
        const auto read  = ::pread(fd_, destination.data(), count, file_offset(offset));
        if (read < 0) {
            if (errno == EINTR) { continue; }
            fail(path_, "pread");
        }
        if (!read) { throw ArtifactError(path_.string() + ": unexpected EOF"); }
        offset += static_cast<std::uint64_t>(read);
        destination = destination.subspan(static_cast<std::size_t>(read));
    }
}

std::size_t InputFile::read_direct(std::uint64_t offset, std::span<std::byte> destination) const {
    if (offset % kPayloadAlignment || destination.size() % kPayloadAlignment ||
        reinterpret_cast<std::uintptr_t>(destination.data()) % kPayloadAlignment ||
        destination.size() > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())) {
        throw ArtifactError(path_.string() + ": unaligned or oversized direct read");
    }
    if (destination.empty()) { return 0; }
    if (direct_fd_ < 0) {
        direct_fd_ = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECT);
        if (direct_fd_ < 0) { fail(path_, "open direct"); }
    }
    ssize_t read;
    do {
        read = ::pread(direct_fd_, destination.data(), destination.size(), file_offset(offset));
    } while (read < 0 && errno == EINTR);
    if (read < 0) { fail(path_, "direct pread"); }
    return static_cast<std::size_t>(read);
}

#endif

} // namespace ninfer::artifact
