#pragma once

#include "backend_usb/usbds.hpp"
#include "backend_usb/usb_api.hpp"

#include <vector>
#include <memory>
#include <string_view>
#include <switch.h>

namespace GooniesInstaller::backend_usb::dump {

struct Usb {
    Usb(u64 transfer_timeout);
    ~Usb();

    Result Write(const void* buf, u64 off, u32 size);
    void SignalCancel();

    // waits for connection and then sends file list.
    Result IsUsbConnected(u64 timeout);
    Result WaitForConnection(std::string_view path, u64 timeout);

    // Result OpenFile(u32 index, s64& file_size);
    Result CloseFile();

    auto GetOpenResult() const {
        return m_open_result;
    }

    auto GetCancelEvent() {
        return m_usb->GetCancelEvent();
    }

private:
    Result SendAndVerify(const void* data, u32 size, u64 timeout, api::ResultPacket* out = nullptr);
    Result SendAndVerify(const void* data, u32 size, api::ResultPacket* out = nullptr);

private:
    std::unique_ptr<backend_usb::UsbDs> m_usb{};
    Result m_open_result{};
    bool m_was_connected{};
};

} // namespace GooniesInstaller::backend_usb::dumpl
