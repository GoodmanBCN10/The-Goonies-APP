#include "haze/threaded_file_transfer.hpp"
#include "haze/thread.hpp"
#include "haze/log.hpp"
// can't include because circular dependency :/
// #include "haze/ptp_responder_types.hpp"

#include <vector>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <new>

namespace sphaira::thread {
namespace {

constexpr u64 BUFFER_SIZE_ALLOC = std::max(BUFFER_SIZE_READ, BUFFER_SIZE_WRITE);

struct ScopedMutex {
    ScopedMutex(Mutex* mutex) : m_mutex{mutex} {
        mutexLock(m_mutex);
    }
    ~ScopedMutex() {
        mutexUnlock(m_mutex);
    }

    ScopedMutex(const ScopedMutex&) = delete;
    void operator=(const ScopedMutex&) = delete;

private:
    Mutex* const m_mutex;
};

#define SCOPED_MUTEX(_m) ScopedMutex ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){_m}

struct ThreadBuffer {
    ThreadBuffer() {
        buf.reserve(BUFFER_SIZE_ALLOC);
    }

    std::vector<u8> buf;
    s64 off;
};

template<std::size_t Size>
struct RingBuf {
private:
    ThreadBuffer buf[Size]{};
    unsigned r_index{};
    unsigned w_index{};

    static_assert((sizeof(RingBuf::buf) & (sizeof(RingBuf::buf) - 1)) == 0, "Must be power of 2!");

public:
    void ringbuf_reset() {
        this->r_index = this->w_index;
    }

    unsigned ringbuf_capacity() const {
        return sizeof(this->buf) / sizeof(this->buf[0]);
    }

    unsigned ringbuf_size() const {
        return (this->w_index - this->r_index) % (ringbuf_capacity() * 2U);
    }

    unsigned ringbuf_free() const {
        return ringbuf_capacity() - ringbuf_size();
    }

    void ringbuf_push(std::vector<u8>& buf_in, s64 off_in) {
        auto& value = this->buf[this->w_index % ringbuf_capacity()];
        value.off = off_in;
        std::swap(value.buf, buf_in);

        this->w_index = (this->w_index + 1U) % (ringbuf_capacity() * 2U);
    }

    void ringbuf_pop(std::vector<u8>& buf_out, s64& off_out) {
        auto& value = this->buf[this->r_index % ringbuf_capacity()];
        off_out = value.off;
        std::swap(value.buf, buf_out);

        this->r_index = (this->r_index + 1U) % (ringbuf_capacity() * 2U);
    }
};

struct ThreadData {
    ThreadData(UEvent& _uevent, s64 size, const ReadCallback& _rfunc, const WriteCallback& _wfunc, u64 buffer_size);

    auto GetResults() volatile -> Result;
    void WakeAllThreads();

    void SetReadResult(Result result) {
        read_result = result;
        if (R_FAILED(result)) {
            ueventSignal(&uevent);
        }
    }

    void SetWriteResult(Result result) {
        write_result = result;
        ueventSignal(&uevent);
    }

    auto GetWriteOffset() volatile const -> s64 {
        return write_offset;
    }

    auto GetWriteSize() const {
        return write_size;
    }

    Result readFuncInternal();
    Result writeFuncInternal();

private:
    bool IsWriteBufFull();
    Result SetWriteBuf(std::vector<u8>& buf, s64 size);
    Result GetWriteBuf(std::vector<u8>& buf_out, s64& off_out);

    Result Read(void* buf, s64 size, u64* bytes_read);

private:
    // these need to be copied
    UEvent& uevent;
    const ReadCallback& rfunc;
    const WriteCallback& wfunc;

    // these need to be created
    Mutex mutex{};

    CondVar can_read{};
    CondVar can_write{};

    // ajdust if needed.
    // using 1 as internally its triple buffered anyway (r/w have their own buffers).
    RingBuf<1> write_buffers{};

    const u64 read_buffer_size;
    const s64 write_size;

    // these are shared between threads
    std::atomic<s64> read_offset{};
    std::atomic<s64> write_offset{};

    std::atomic<Result> read_result{Result::SuccessValue};
    std::atomic<Result> write_result{Result::SuccessValue};

    std::atomic_bool read_running{true};
    std::atomic_bool write_running{true};
};

ThreadData::ThreadData(UEvent& _uevent, s64 size, const ReadCallback& _rfunc, const WriteCallback& _wfunc, u64 buffer_size)
: uevent{_uevent}
, rfunc{_rfunc}
, wfunc{_wfunc}
, read_buffer_size{buffer_size}
, write_size{size} {
    mutexInit(std::addressof(mutex));

    condvarInit(std::addressof(can_read));
    condvarInit(std::addressof(can_write));
}

auto ThreadData::GetResults() volatile -> Result {
    R_TRY(read_result.load());
    R_TRY(write_result.load());
    R_SUCCEED();
}

void ThreadData::WakeAllThreads() {
    condvarWakeAll(std::addressof(can_read));
    condvarWakeAll(std::addressof(can_write));

    mutexUnlock(std::addressof(mutex));
}

bool ThreadData::IsWriteBufFull() {
    SCOPED_MUTEX(std::addressof(mutex));

    // use condvar instead of waiting a set time as the buffer may be freed immediately.
    // however, to avoid deadlocks, we still need a timeout
    if (!write_buffers.ringbuf_free()) {
        if (R_FAILED(condvarWaitTimeout(std::addressof(can_read), std::addressof(mutex), 1e+8))) { // 100ms
            return true;
        }
    }

    return !write_buffers.ringbuf_free();
}

Result ThreadData::SetWriteBuf(std::vector<u8>& buf, s64 size) {
    buf.resize(size);

    SCOPED_MUTEX(std::addressof(mutex));
    if (!write_buffers.ringbuf_free()) {
        if (!write_running) {
            R_SUCCEED();
        }

        haze::log_write("SetWriteBuf: waiting for space...\n");
        R_TRY(condvarWait(std::addressof(can_read), std::addressof(mutex)));
        haze::log_write("SetWriteBuf: got space!\n");
    }

    R_TRY(GetResults());
    write_buffers.ringbuf_push(buf, 0);
    return condvarWakeOne(std::addressof(can_write));
}

Result ThreadData::GetWriteBuf(std::vector<u8>& buf_out, s64& off_out) {
    SCOPED_MUTEX(std::addressof(mutex));
    if (!write_buffers.ringbuf_size()) {
        if (!read_running) {
            buf_out.resize(0);
            R_SUCCEED();
        }

        haze::log_write("GetWriteBuf: waiting for data...\n");
        R_TRY(condvarWait(std::addressof(can_write), std::addressof(mutex)));
        haze::log_write("GetWriteBuf: got data!\n");
    }

    R_TRY(GetResults());
    write_buffers.ringbuf_pop(buf_out, off_out);
    return condvarWakeOne(std::addressof(can_read));
}

Result ThreadData::Read(void* buf, s64 size, u64* bytes_read) {
    size = std::min<s64>(size, write_size - read_offset);
    const auto rc = rfunc(buf, read_offset, size, bytes_read);
    read_offset += *bytes_read;
    return rc;
}

// read thread reads all data from rfunc.
Result ThreadData::readFuncInternal() {
    ON_SCOPE_EXIT{ read_running = false; };

    // the main buffer which data is read into.
    std::vector<u8> buf;
    buf.reserve(this->read_buffer_size);
    bool slow_mode{};

    while (this->read_offset < this->write_size && R_SUCCEEDED(this->GetResults())) {
        // this will wait for max 100ms until the buffer has space.
        const auto is_write_full = this->IsWriteBufFull();

        // check if the write thread returned early, usually due to an error.
        if (is_write_full && !write_running) {
            haze::log_write("ReadFunc: write thread exited, stopping read thread\n");
            break;
        }

        if (!slow_mode && is_write_full) {
            slow_mode = true;
            haze::log_write("ReadFunc: switching to slow mode\n");
        } else if (slow_mode && !is_write_full) {
            slow_mode = false;
            haze::log_write("ReadFunc: switching to fast mode\n");
        }

        s64 read_size = this->read_buffer_size;
        if (slow_mode) {
            // reduce transfer rate in order to prevent windows from freezing.
            read_size = 2048; // UsbBulkSlowModePacketBufferSize.
        }

        const auto buf_offset = buf.size();
        buf.resize(buf_offset + read_size);

        u64 bytes_read{};
        R_TRY(this->Read(buf.data() + buf_offset, read_size, std::addressof(bytes_read)));
        if (!bytes_read) {
            break;
        }

        // resize to actual read size.
        buf.resize(buf_offset + bytes_read);

        // when we have left slow mode, flush.
        // todo: check buffer size so that it doesn't grow too large.
        if (!slow_mode) {
            R_TRY(this->SetWriteBuf(buf, buf.size()));
            buf.clear();
        }
    }

    // flush buffer if needed.
    if (!buf.empty()) {
        haze::log_write("ReadFunc: flushing final buffer of size %zu\n", buf.size());
        R_TRY(this->SetWriteBuf(buf, buf.size()));
    }

    R_SUCCEED();
}

// write thread writes data to wfunc.
Result ThreadData::writeFuncInternal() {
    ON_SCOPE_EXIT{ write_running = false; };

    std::vector<u8> buf;
    buf.reserve(this->read_buffer_size);

    while (this->write_offset < this->write_size && R_SUCCEEDED(this->GetResults())) {
        s64 dummy_off;
        R_TRY(this->GetWriteBuf(buf, dummy_off));
        const auto size = buf.size();
        if (!size) {
            break;
        }

        R_TRY(this->wfunc(buf.data(), this->write_offset, buf.size()));
        this->write_offset += size;
    }

    R_SUCCEED();
}

void readFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetReadResult(t->readFuncInternal());
}

void writeFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetWriteResult(t->writeFuncInternal());
}

Result TransferInternal(s64 size, const ReadCallback& rfunc, const WriteCallback& wfunc, u64 buffer_size, Mode mode) {
    if (mode == Mode::SingleThreadedIfSmaller) {
        if ((u64)size <= buffer_size) {
            mode = Mode::SingleThreaded;
        } else {
            mode = Mode::MultiThreaded;
        }
    }

    buffer_size = std::min<u64>(size, buffer_size);

    if (mode == Mode::SingleThreaded) {
        haze::log_write("Using single-threaded transfer\n");
        std::vector<u8> buf(buffer_size);

        s64 offset{};
        while (offset < size) {
            u64 bytes_read;
            const auto rsize = std::min<s64>(buf.size(), size - offset);
            R_TRY(rfunc(buf.data(), offset, rsize, &bytes_read));
            if (!bytes_read) {
                break;
            }

            R_TRY(wfunc(buf.data(), offset, bytes_read));

            offset += bytes_read;
        }

        R_SUCCEED();
    }
    else {
        haze::log_write("Using multi-threaded transfer\n");
        UEvent uevent;
        ueventCreate(&uevent, false);
        ThreadData t_data{uevent, size, rfunc, wfunc, buffer_size};

        Thread t_read{};
        R_TRY(utils::CreateThread(&t_read, readFunc, std::addressof(t_data)));
        ON_SCOPE_EXIT { threadClose(&t_read); };

        Thread t_write{};
        R_TRY(utils::CreateThread(&t_write, writeFunc, std::addressof(t_data)));
        ON_SCOPE_EXIT { threadClose(&t_write); };

        R_TRY(threadStart(std::addressof(t_read)));
        R_TRY(threadStart(std::addressof(t_write)));

        ON_SCOPE_EXIT { threadWaitForExit(std::addressof(t_read)); };
        ON_SCOPE_EXIT { threadWaitForExit(std::addressof(t_write)); };

        // waits until either an error or write thread has finished.
        waitSingle(waiterForUEvent(&uevent), UINT64_MAX);
        haze::log_write("One thread finished or error occurred\n");

        // wait for all threads to close.
        for (;;) {
            t_data.WakeAllThreads();

            if (R_FAILED(waitSingleHandle(t_read.handle, 1000))) {
                continue;
            } else if (R_FAILED(waitSingleHandle(t_write.handle, 1000))) {
                continue;
            }
            break;
        }

        haze::log_write("Both threads finished\n");
        R_RETURN(t_data.GetResults());
    }
}

} // namespace

Result Transfer(s64 size, const ReadCallback& rfunc, const WriteCallback& wfunc, u64 buffer_size, Mode mode) {
    return TransferInternal(size, rfunc, wfunc, buffer_size, mode);
}

} // namespace::thread
