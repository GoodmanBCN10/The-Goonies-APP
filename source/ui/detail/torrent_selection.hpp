#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/download_manager.hpp"
#include "app/install_space.hpp"
#include "app/installed_title_service.hpp"
#include "app/package_utils.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/theme.hpp"

namespace goonies::ui {

struct TorrentSelectionEntry {
    std::string path;
    uint64_t length = 0;
    bool package = false;
    bool installed = false;
    std::string kindText;
    FileAction action = FileAction::Download;
};

inline const char* actionLabel(FileAction action) {
    switch (action) {
        case FileAction::Install: return "Install";
        case FileAction::Download: return "Download";
        case FileAction::Skip: return "Skip";
    }
    return "Skip";
}

class TorrentSelectionCell : public brls::RecyclerCell {
public:
    TorrentSelectionCell() {
        setFocusable(true);
        setAxis(brls::Axis::ROW);
        setAlignItems(brls::AlignItems::CENTER);
        setPadding(10, 18, 10, 18);
        setHeight(84);
        setCornerRadius(12.0f);
        setHighlightCornerRadius(12.0f);
        setHideHighlightBorder(true);
        setHideHighlightBackground(true);

        markBox_ = new brls::Box();
        markBox_->setWidth(136);
        markBox_->setHeight(42);
        markBox_->setCornerRadius(8.0f);
        markBox_->setAlignItems(brls::AlignItems::CENTER);
        markBox_->setJustifyContent(brls::JustifyContent::CENTER);
        markBox_->setMarginRight(14);
        markBox_->setFocusable(false);

        mark_ = new brls::Label();
        mark_->setFontSize(13);
        markBox_->addView(mark_);
        addView(markBox_);

        auto* body = new brls::Box(brls::Axis::COLUMN);
        body->setGrow(1);
        body->setJustifyContent(brls::JustifyContent::CENTER);
        body->setFocusable(false);

        title_ = new brls::Label();
        title_->setSingleLine(true);
        title_->setFontSize(17);
        body->addView(title_);

        meta_ = new brls::Label();
        meta_->setFontSize(13);
        meta_->setMarginTop(4);
        meta_->setTextColor(theme::textTertiary());
        body->addView(meta_);

        addView(body);
    }

    void setEntry(const TorrentSelectionEntry& entry) {
        action_ = entry.action;
        title_->setText(entry.path);

        if (entry.action == FileAction::Install) {
            markBox_->setBackgroundColor(nvgRGBA(16, 185, 129, 50));
            markBox_->setBorderThickness(1.5f);
            markBox_->setBorderColor(nvgRGBA(16, 185, 129, 220));
            mark_->setText(t("✓  INSTALAR", "✓  INSTALL", "✓  INSTALAR"));
            mark_->setTextColor(nvgRGB(16, 225, 140));
            title_->setTextColor(nvgRGB(255, 255, 255));
        } else if (entry.action == FileAction::Download) {
            markBox_->setBackgroundColor(nvgRGBA(0, 210, 247, 45));
            markBox_->setBorderThickness(1.5f);
            markBox_->setBorderColor(nvgRGBA(0, 210, 247, 220));
            mark_->setText(t("↓  DESCARGAR", "↓  DOWNLOAD", "↓  BAIXAR"));
            mark_->setTextColor(theme::neonCyan());
            title_->setTextColor(nvgRGB(255, 255, 255));
        } else {
            markBox_->setBackgroundColor(nvgRGBA(255, 255, 255, 8));
            markBox_->setBorderThickness(1.0f);
            markBox_->setBorderColor(nvgRGBA(255, 255, 255, 20));
            mark_->setText(t("✕  OMITIR", "✕  SKIP", "✕  OMITIR"));
            mark_->setTextColor(nvgRGB(140, 145, 160));
            title_->setTextColor(nvgRGB(150, 155, 168));
        }

        std::string meta = formatBytes(entry.length) + "   ·   ";
        if (entry.package) {
            meta += entry.kindText.empty() ? "NSP/NSZ" : entry.kindText;
        } else {
            meta += t("Archivo extra", "Extra file", "Arquivo extra");
        }
        if (entry.installed) {
            meta += t("   ·   [ YA INSTALADO ]", "   ·   [ ALREADY INSTALLED ]", "   ·   [ JA INSTALADO ]");
        }
        meta_->setText(meta);
        meta_->setTextColor(entry.installed ? theme::gold() : theme::textTertiary());
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        bool focused = isFocused();
        float r = 12.0f;

        if (focused) {
            NVGcolor glowColor = (action_ == FileAction::Install) ? nvgRGB(16, 185, 129) : theme::neonCyan();
            NVGpaint glow = nvgBoxGradient(vg, x, y, width, height, r, 12.0f,
                                           nvgRGBAf(glowColor.r, glowColor.g, glowColor.b, 0.40f),
                                           nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(vg);
            nvgRect(vg, x - 8, y - 8, width + 16, height + 16);
            nvgFillPaint(vg, glow);
            nvgFill(vg);
        }

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, r);
        if (action_ == FileAction::Install) {
            nvgFillColor(vg, focused ? nvgRGBA(16, 185, 129, 45) : nvgRGBA(16, 185, 129, 24));
        } else if (action_ == FileAction::Download) {
            nvgFillColor(vg, focused ? nvgRGBA(0, 210, 247, 45) : nvgRGBA(0, 210, 247, 24));
        } else {
            nvgFillColor(vg, focused ? nvgRGBA(30, 34, 46, 240) : nvgRGBA(18, 21, 29, 220));
        }
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, r);
        if (focused) {
            nvgStrokeWidth(vg, 2.5f);
            nvgStrokeColor(vg, (action_ == FileAction::Install) ? nvgRGB(16, 225, 140) : theme::neonCyan());
        } else {
            nvgStrokeWidth(vg, 1.0f);
            if (action_ == FileAction::Install) {
                nvgStrokeColor(vg, nvgRGBA(16, 185, 129, 120));
            } else if (action_ == FileAction::Download) {
                nvgStrokeColor(vg, nvgRGBA(0, 210, 247, 120));
            } else {
                nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 12));
            }
        }
        nvgStroke(vg);

        brls::RecyclerCell::draw(vg, x, y, width, height, style, ctx);
    }

private:
    brls::Box* markBox_;
    brls::Label* mark_;
    brls::Label* title_;
    brls::Label* meta_;
    FileAction action_ = FileAction::Download;
};

class TorrentSelectionActivity;

class TorrentSelectionDataSource : public brls::RecyclerDataSource {
public:
    explicit TorrentSelectionDataSource(TorrentSelectionActivity* owner)
        : owner_(owner) {}

    void setEntries(std::vector<TorrentSelectionEntry> entries) {
        entries_ = std::move(entries);
    }

    void setAll(bool selected) {
        for (auto& entry : entries_) {
            entry.action = selected
                ? (entry.package ? FileAction::Install : FileAction::Download)
                : FileAction::Skip;
        }
    }

    size_t selectedCount() const {
        size_t count = 0;
        for (const auto& entry : entries_)
            if (entry.action != FileAction::Skip)
                ++count;
        return count;
    }

    size_t installCount() const {
        size_t count = 0;
        for (const auto& entry : entries_)
            if (entry.action == FileAction::Install)
                ++count;
        return count;
    }

    size_t downloadCount() const {
        size_t count = 0;
        for (const auto& entry : entries_)
            if (entry.action == FileAction::Download)
                ++count;
        return count;
    }

    std::vector<uint8_t> fileActions() const {
        std::vector<uint8_t> actions;
        actions.reserve(entries_.size());
        for (const auto& entry : entries_) {
            actions.push_back(static_cast<uint8_t>(entry.action));
        }
        return actions;
    }

    int numberOfSections(brls::RecyclerFrame*) override {
        return entries_.empty() ? 1 : 1;
    }

    int numberOfRows(brls::RecyclerFrame*, int) override {
        return entries_.empty() ? 1 : static_cast<int>(entries_.size());
    }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler,
                                   brls::IndexPath index) override;

    void didSelectRowAt(brls::RecyclerFrame*, brls::IndexPath index) override;

    const TorrentSelectionEntry* entryAt(int row) const {
        if (row < 0 || static_cast<size_t>(row) >= entries_.size())
            return nullptr;
        return &entries_[static_cast<size_t>(row)];
    }

    void cycleRow(int row);

private:
    TorrentSelectionActivity* owner_;
    std::vector<TorrentSelectionEntry> entries_;
};

class TorrentSelectionActivity : public brls::Activity {
public:
    friend class TorrentSelectionDataSource;

    TorrentSelectionActivity(DownloadManager* manager, std::string path,
                             goonies::TorrentPreview preview,
                             TransferMode preferred,
                             StreamSelection initialSelection,
                             std::vector<uint8_t> initialPeers = {},
                             InstalledTitleService* installed = nullptr,
                             uint64_t appId = 0)
        : manager_(manager), path_(std::move(path)),
          preview_(std::move(preview)), preferred_(preferred),
          initialSelection_(initialSelection),
          initialPeers_(std::move(initialPeers)),
          installed_(installed), appId_(appId) {
        auto* content = new brls::Box(brls::Axis::COLUMN);
        content->setGrow(1);
        content->setPadding(24, 38, 24, 34);
        content->setBackgroundColor(theme::overlay());
        content->setCornerRadius(12);

        title_ = new brls::Label();
        title_->setFontSize(26);
        title_->setText(t("Selecciona archivos a descargar", "Select files to download", "Selecionar arquivos para baixar"));
        content->addView(title_);

        summary_ = new brls::Label();
        summary_->setFontSize(15);
        summary_->setMarginTop(8);
        summary_->setMarginBottom(14);
        summary_->setTextColor(theme::textSecondary());
        summary_->setText(t("Pulsa (A) para alternar accion: [✓ Instalar] -> [↓ Descargar] -> [✕ Omitir]",
                            "Press (A) to toggle: [✓ Install] -> [↓ Download] -> [✕ Skip]",
                            "Pressione (A) para alternar: [✓ Instalar] -> [↓ Baixar] -> [✕ Omitir]"));
        content->addView(summary_);

        recycler_ = new brls::RecyclerFrame();
        recycler_->setGrow(1);
        recycler_->setPadding(6, 0, 6, 0);
        recycler_->estimatedRowHeight = 84;
        recycler_->registerCell("FileSelect",
                               [] { return new TorrentSelectionCell(); });
        dataSource_ = new TorrentSelectionDataSource(this);
        recycler_->setDataSource(dataSource_);
        content->addView(recycler_);

        auto* buttons = new brls::Box(brls::Axis::COLUMN);
        buttons->setMarginTop(16);

        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(10);

        selectAll_ = new brls::Button();
        selectAll_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        selectAll_->setFontSize(16);
        selectAll_->setHeight(50);
        selectAll_->setCornerRadius(10.0f);
        selectAll_->setMarginRight(10);
        selectAll_->setGrow(1);
        selectAll_->setText(t("Seleccionar todo", "Select all", "Selecionar tudo"));
        selectAll_->registerClickAction([this](brls::View*) {
            setAllSelected(true);
            return true;
        });
        row->addView(selectAll_);

        clearAll_ = new brls::Button();
        clearAll_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        clearAll_->setFontSize(16);
        clearAll_->setHeight(50);
        clearAll_->setCornerRadius(10.0f);
        clearAll_->setGrow(1);
        clearAll_->setText(t("Deseleccionar todo", "Clear all", "Limpar tudo"));
        clearAll_->registerClickAction([this](brls::View*) {
            setAllSelected(false);
            return true;
        });
        row->addView(clearAll_);

        buttons->addView(row);

        installSelected_ = new brls::Button();
        installSelected_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        installSelected_->setFontSize(18);
        installSelected_->setHeight(56);
        installSelected_->setCornerRadius(12.0f);
        installSelected_->setText(t("Confirmar y continuar", "Continue", "Confirmar e continuar"));
        installSelected_->registerClickAction([this](brls::View*) {
            confirmSelection();
            return true;
        });
        buttons->addView(installSelected_);

        content->addView(buttons);
        frame_ = new brls::AppletFrame(content);
        frame_->setTitle(preview_.name.empty() ? t("Selecciona archivos a descargar", "Select files to download", "Selecionar arquivos para baixar")
                                              : preview_.name);

        populateEntries();
        refreshSummary();
    }

    ~TorrentSelectionActivity() override {
        if (!finished_ && !path_.empty())
            ::unlink(path_.c_str());
    }

    brls::View* createContentView() override {
        return frame_;
    }

    void onContentAvailable() override {
        registerAction(t("Seleccionar todo", "Select all", "Selecionar tudo"), brls::BUTTON_X, [this](brls::View*) {
            setAllSelected(true);
            return true;
        });
        registerAction(t("Deseleccionar todo", "Clear all", "Limpar tudo"), brls::BUTTON_Y, [this](brls::View*) {
            setAllSelected(false);
            return true;
        });
        registerAction(t("Continuar", "Continue", "Continuar"), brls::BUTTON_RB,
                       [this](brls::View*) {
                           confirmSelection();
                           return true;
                       });
    }

private:
    void populateEntries() {
        uint64_t effectiveAppId = appId_;
        if (effectiveAppId == 0) {
            effectiveAppId = parseTitleIdFromFileName(preview_.name);
        }

        std::vector<TorrentSelectionEntry> entries;
        entries.reserve(preview_.files.size());
        for (const auto& file : preview_.files) {
            TorrentSelectionEntry entry;
            entry.path = file.path;
            entry.length = file.length;
            entry.package = file.package;
            if (file.package && installed_) {
                PackageInfo pkg = classifyPackage(file.path, effectiveAppId);
                entry.installed = installed_->isPackageInstalled(pkg);
                if (pkg.kind == PackageKind::Update) {
                    entry.kindText = "Update";
                } else if (pkg.kind == PackageKind::DLC) {
                    entry.kindText = "DLC";
                } else if (pkg.kind == PackageKind::BaseGame) {
                    entry.kindText = "Base";
                }
            }

            if (preferred_ == TransferMode::StreamInstall && file.package) {
                entry.action = entry.installed ? FileAction::Skip : FileAction::Install;
            } else if (initialSelection_ == StreamSelection::AllFiles ||
                       preferred_ != TransferMode::StreamInstall) {
                entry.action = entry.installed ? FileAction::Skip : FileAction::Download;
            } else {
                entry.action = FileAction::Skip;
            }
            entries.push_back(std::move(entry));
        }
        dataSource_->setEntries(std::move(entries));
        recycler_->reloadData();
    }

    void setAllSelected(bool selected) {
        dataSource_->setAll(selected);
        recycler_->reloadData();
        refreshSummary();
    }

    void refreshSummary() {
        size_t selected = dataSource_->selectedCount();
        size_t installs = dataSource_->installCount();
        size_t downloads = dataSource_->downloadCount();
        std::vector<uint8_t> actions = dataSource_->fileActions();
        const TransferMode mode = installs > 0
            ? TransferMode::StreamInstall
            : TransferMode::DownloadOnly;
        const auto estimate = goonies::estimateInstallSpace(preview_, actions,
                                                            mode);
        const StorageSpaceSnapshot storage =
            goonies::queryStorageSpace(manager_->rootPath());
        const auto check = goonies::assessInstallSpace(estimate, storage);
        std::string text = std::to_string(selected) + " / " +
                           std::to_string(preview_.files.size()) + " " +
                           t("archivos", "files", "arquivos") + "   (" +
                           formatBytes(estimate.requiredBytes) + ")";
        text += storage.available
            ? std::string(t("   SD libre: ", "   SD free: ", "   SD livre: ")) + formatBytes(storage.freeBytes)
            : std::string(t("   SD libre: no disponible", "   SD free: unavailable", "   SD livre: indisponivel"));
        if (installs > 0) {
            text += std::string(t("   Instalar: ", "   Install: ", "   Instalar: ")) + std::to_string(installs);
            if (downloads > 0)
                text += std::string(t("   Descargar: ", "   Download: ", "   Baixar: ")) + std::to_string(downloads);
        } else {
            text += std::string(t("   (Solo descarga)", "   (Download-only)", "   (Apenas download)"));
        }
        if (estimate.certainty ==
            SpaceEstimateCertainty::CompressedUnknown) {
            text += t("   (NSZ comprimido)", "   (NSZ may expand)", "   (NSZ pode expandir)");
        }
        if (check.status == InstallSpaceCheckStatus::Insufficient)
            text += std::string(t("   Faltan ", "   Need ", "   Faltam ")) + formatBytes(check.shortfallBytes);
        summary_->setText(text);
        installSelected_->setState(selected == 0 || estimate.overflow ||
                                    check.status ==
                                        InstallSpaceCheckStatus::Insufficient
            ? brls::ButtonState::DISABLED
            : brls::ButtonState::ENABLED);
    }

    void confirmSelection() {
        std::vector<uint8_t> actions = dataSource_->fileActions();
        if (actions.empty() && preview_.files.empty()) {
            brls::Application::notify(t("No se encontraron archivos en este torrent.", "No files found in this torrent.", "Nenhum arquivo encontrado."));
            return;
        }
        size_t selected = dataSource_->selectedCount();
        if (selected == 0) {
            brls::Application::notify(t("Selecciona al menos un archivo para continuar.", "Select at least one file.", "Selecione pelo menos um arquivo."));
            return;
        }

        TransferMode mode = dataSource_->installCount() > 0
            ? TransferMode::StreamInstall
            : TransferMode::DownloadOnly;
        const auto estimate = goonies::estimateInstallSpace(preview_, actions,
                                                            mode);
        const StorageSpaceSnapshot storage =
            goonies::queryStorageSpace(manager_->rootPath());
        if (goonies::assessInstallSpace(estimate, storage).status ==
            InstallSpaceCheckStatus::Insufficient) {
            refreshSummary();
            brls::Application::notify(t("Espacio insuficiente en la tarjeta microSD.", "Not enough free space on SD.", "Espaco insuficiente no microSD."));
            return;
        }
        std::string id;
        std::string error;
        if (!manager_->importTorrentActions(path_, actions, id, error,
                                            initialPeers_)) {
            brls::Application::notify(error);
            return;
        }
        ::unlink(path_.c_str());
        finished_ = true;
        brls::Application::notify(mode == TransferMode::StreamInstall
            ? t("Anadido. Instalando en la microSD...", "Added. Installing to SD...", "Adicionado. Instalando no microSD...")
            : t("Anadido a la cola de descargas.", "Added to downloads.", "Adicionado a fila de downloads."));
        brls::Application::popActivity();
    }

    DownloadManager* manager_;
    std::string path_;
    goonies::TorrentPreview preview_;
    TransferMode preferred_;
    StreamSelection initialSelection_;
    std::vector<uint8_t> initialPeers_;
    InstalledTitleService* installed_ = nullptr;
    uint64_t appId_ = 0;
    brls::AppletFrame* frame_ = nullptr;
    brls::Label* title_ = nullptr;
    brls::Label* summary_ = nullptr;
    brls::RecyclerFrame* recycler_ = nullptr;
    TorrentSelectionDataSource* dataSource_ = nullptr;
    brls::Button* selectAll_ = nullptr;
    brls::Button* clearAll_ = nullptr;
    brls::Button* installSelected_ = nullptr;
    bool finished_ = false;
};

}  // namespace goonies::ui
