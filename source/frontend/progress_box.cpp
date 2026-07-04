#include "frontend/progress_box.hpp"
#include "frontend/option_box.hpp"
#include "frontend/nvg_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "threaded_file_transfer.hpp"
#include "i18n.hpp"

#include "core_utils/utils.hpp"
#include "core_utils/thread.hpp"

#include <cstring>
#include <cmath>

namespace GooniesInstaller::frontend {
namespace {

void threadFunc(void* arg) {
    auto d = static_cast<ProgressBox::ThreadData*>(arg);
    d->result = d->callback(d->pbox);
    d->pbox->RequestExit();
}

} // namespace

ProgressBox::ProgressBox(int image, const std::string& action, const std::string& title, const ProgressBoxCallback& callback, const ProgressBoxDoneCallback& done)
: m_done{done}
, m_action{action}
, m_title{title}
, m_image{image} {
    App::SetAutoSleepDisabled(true);
    if (App::GetApp()->m_progress_boost_mode.Get()) {
        App::SetBoostMode(true);
    }

    SetAction(Button::B, Action{"Back"_i18n, [this](){
        App::Push<OptionBox>("Are you sure you wish to cancel?"_i18n, "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
            if (op_index && *op_index) {
                RequestExit();
                SetPop();
            }
        });
    }});

    m_pos.w = SCREEN_WIDTH - 200.f;
    m_pos.h = 130.f;
    m_pos.x = 100.f;
    m_pos.y = 40.f;

    // create cancel event.
    ueventCreate(&m_uevent, false);

    m_thread_data.pbox = this;
    m_thread_data.callback = callback;
    if (R_FAILED(core_utils::CreateThread(&m_thread, threadFunc, &m_thread_data))) {
        log_write("failed to create thead\n");
    }
    if (R_FAILED(threadStart(&m_thread))) {
        log_write("failed to start thread\n");
    }
}

ProgressBox::~ProgressBox() {
    ueventSignal(GetCancelEvent());
    m_stop_source.request_stop();

    if (R_FAILED(threadWaitForExit(&m_thread))) {
        log_write("failed to join thread\n");
    }
    if (R_FAILED(threadClose(&m_thread))) {
        log_write("failed to close thread\n");
    }

    FreeImage();
    if (m_done) {
        m_done(m_thread_data.result);
    }

    App::SetBoostMode(false);
    App::SetAutoSleepDisabled(false);
}

auto ProgressBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (ShouldExit()) {
        SetPop();
    }
}

auto ProgressBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    mutexLock(&m_mutex);
    std::vector<u8> image_data{};
    std::swap(m_image_data, image_data);
    if (m_timestamp.GetSeconds()) {
        m_timestamp.Update();
        m_speed = m_offset - m_last_offset;
        m_last_offset = m_offset;
    }

    const auto action = m_action;
    const auto title = m_title;
    const auto transfer = m_transfer;
    const auto size = m_size;
    const auto offset = m_offset;
    const auto speed = m_speed;
    const auto last_offset = m_last_offset;
    auto image = m_image;

    if (m_is_image_pending) {
        FreeImage();
        image = m_image = m_image_pending;
        m_image_pending = 0;
        m_is_image_pending = false;
    }
    mutexUnlock(&m_mutex);

    if (!image_data.empty()) {
        FreeImage();
        image = m_image = nvgCreateImageMem(vg, 0, image_data.data(), image_data.size());
        m_own_image = true;
    }

    // Removed dimBackground so bottom pill is visible
    nvgBeginPath(vg);
    nvgRoundedRect(vg, m_pos.x, m_pos.y, m_pos.w, m_pos.h, 15.f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 15));
    nvgFill(vg);

    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 40));
    nvgStrokeWidth(vg, 2.f);
    nvgStroke(vg);

    const auto center_x = m_pos.x + m_pos.w/2;
    const auto end_y = m_pos.y + m_pos.h;
    
    // Wider progress bar since we removed the image
    const auto progress_bar_w = m_pos.w - 60.f;
    const Vec4 prog_bar = { center_x - progress_bar_w / 2, m_pos.y + 85.f, progress_bar_w, 8.f };

    nvgSave(vg);
    nvgIntersectScissor(vg, GetX(), GetY(), GetW(), GetH());

    // shapes.
    if (offset && size) {
        const auto font_size = 18.F;
        const auto pad = 15.F;
        const float rounding = 4.f;

        gfx::drawRect(vg, prog_bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), rounding);
        
        // Prevent div by 0 and limit to 100%
        double ratio = (double)offset / (double)size;
        if (ratio > 1.0) ratio = 1.0;
        if (ratio < 0.0) ratio = 0.0;
        const u32 percentage = ratio * 100.0;
        
        gfx::drawRect(vg, prog_bar.x, prog_bar.y, ratio * prog_bar.w, prog_bar.h, theme->GetColour(ThemeEntryID_PROGRESSBAR), rounding);
        
        gfx::drawTextArgs(vg, prog_bar.x, prog_bar.y + prog_bar.h + 15.f, 16, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "%u%%", percentage);

        char off_str[32];
        std::snprintf(off_str, sizeof(off_str), "%.1f MB", (float)offset / 1024.f / 1024.f);
        gfx::drawTextArgs(vg, prog_bar.x + 50.f, prog_bar.y + prog_bar.h + 15.f, 16, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", off_str);

        char tot_str[32];
        std::snprintf(tot_str, sizeof(tot_str), "/ %.1f MB", (float)size / 1024.f / 1024.f);
        gfx::drawTextArgs(vg, prog_bar.x + 140.f, prog_bar.y + prog_bar.h + 15.f, 16, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", tot_str);

        char speed_str[64];
        std::snprintf(speed_str, sizeof(speed_str), "%s/s", core_utils::formatSizeNetwork(speed).c_str());
        gfx::drawTextArgs(vg, prog_bar.x + prog_bar.w, prog_bar.y + prog_bar.h + 15.f, 16, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", speed_str);
    }

    gfx::drawTextArgs(vg, m_pos.x + 30.f, m_pos.y + 30.f, 26, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", action.c_str());

    const auto draw_text = [&](ScrollingText& scroll, const std::string& txt, float y, float size, float pad, ThemeEntryID id){
        scroll.Draw(vg, true, m_pos.x + 30.f, y, m_pos.w - 60.f, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(id), txt);
    };

    if (!transfer.empty()) {
        std::string transfer_text = "Recibiendo: " + transfer;
        draw_text(m_scroll_transfer, transfer_text, m_pos.y + 60.f, 20, 0, ThemeEntryID_TEXT_INFO);
    } else {
        draw_text(m_scroll_title, title, m_pos.y + 60.f, 20, 0, ThemeEntryID_TEXT);
    }

    nvgRestore(vg);
}

auto ProgressBox::SetActionName(const std::string& action)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_action = action;
    return *this;
}

auto ProgressBox::SetTitle(const std::string& title)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_title = title;
    return *this;
}

auto ProgressBox::NewTransfer(const std::string& transfer)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_transfer = transfer;
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_timestamp.Update();
    return *this;
}

auto ProgressBox::UpdateTransferName(const std::string& transfer)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_transfer = transfer;
    return *this;
}

auto ProgressBox::ResetTranfser() -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_timestamp.Update();
    return *this;
}

auto ProgressBox::UpdateTransfer(s64 offset, s64 size)  -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_size = size;
    m_offset = offset;
    return *this;
}

auto ProgressBox::SetImage(int image) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_image_pending = image;
    m_is_image_pending = true;
    return *this;
}

auto ProgressBox::SetImageData(std::vector<u8>& data) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    std::swap(m_image_data, data);
    return *this;
}

auto ProgressBox::SetImageDataConst(std::span<const u8> data) -> ProgressBox& {
    SCOPED_MUTEX(&m_mutex);
    m_image_data.resize(data.size());
    std::memcpy(m_image_data.data(), data.data(), m_image_data.size());
    return *this;
}

void ProgressBox::RequestExit() {
    SCOPED_MUTEX(&m_mutex);
    m_stop_source.request_stop();
    ueventSignal(GetCancelEvent());

    // cancel any registered events.
    for (auto& e : m_cancel_events) {
        ueventSignal(e);
    }
}

auto ProgressBox::ShouldExit() -> bool {
    return m_stop_source.stop_requested();
}

auto ProgressBox::ShouldExitResult() -> Result {
    if (ShouldExit()) {
        R_THROW(Result_TransferCancelled);
    }
    R_SUCCEED();
}

void ProgressBox::AddCancelEvent(UEvent* event) {
    if (!event) {
        return;
    }

    SCOPED_MUTEX(&m_mutex);
    if (std::ranges::find(m_cancel_events, event) == m_cancel_events.end()) {
        m_cancel_events.emplace_back(event);
    }
}

void ProgressBox::RemoveCancelEvent(const UEvent* event) {
    if (!event) {
        return;
    }

    SCOPED_MUTEX(&m_mutex);
    m_cancel_events.erase(std::remove(m_cancel_events.begin(), m_cancel_events.end(), event), m_cancel_events.end());
}

auto ProgressBox::CopyFile(fs::Fs* fs_src, fs::Fs* fs_dst, const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    const auto is_file_based_emummc = App::IsFileBaseEmummc();
    const auto is_both_native = fs_src->IsNative() && fs_dst->IsNative();

    fs::File src_file;
    R_TRY(fs_src->OpenFile(src_path, FsOpenMode_Read, &src_file));

    s64 src_size;
    R_TRY(src_file.GetSize(&src_size));

    // this can fail if it already exists so we ignore the result.
    // if the file actually failed to be created, the result is implicitly
    // handled when we try and open it for writing.
    fs_dst->CreateFile(dst_path, src_size, 0);

    fs::File dst_file;
    R_TRY(fs_dst->OpenFile(dst_path, FsOpenMode_Write, &dst_file));
    R_TRY(dst_file.SetSize(src_size));

    R_TRY(thread::Transfer(this, src_size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto rc = src_file.Read(off, data, size, 0, bytes_read);

            if (is_both_native && is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }

            return rc;
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            const auto rc = dst_file.Write(off, data, size, 0);

            if (is_both_native && is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }

            return rc;
        }, single_threaded ? thread::Mode::SingleThreaded : thread::Mode::MultiThreaded
    ));

    R_SUCCEED();
}

auto ProgressBox::CopyFile(fs::Fs* fs, const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    return CopyFile(fs, fs, src_path, dst_path, single_threaded);
}

auto ProgressBox::CopyFile(const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    return CopyFile(&fs, src_path, dst_path, single_threaded);
}

void ProgressBox::Yield() {
    svcSleepThread(YieldType_WithoutCoreMigration);
}

void ProgressBox::FreeImage() {
    if (m_image && m_own_image) {
        nvgDeleteImage(App::GetVg(), m_image);
    }

    m_image = 0;
    m_own_image = false;
}

} // namespace GooniesInstaller::frontend
