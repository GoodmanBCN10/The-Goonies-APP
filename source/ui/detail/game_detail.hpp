#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "app/app_settings.hpp"
#include "app/catalog_presentation.hpp"
#include "app/catalog_service.hpp"
#include "app/download_manager.hpp"
#include "app/game_metadata_service.hpp"
#include "app/install_space.hpp"
#include "app/installed_title_service.hpp"
#include "app/magnet_resolver.hpp"
#include "app/nx_versions_service.hpp"
#include "app/update_dismiss_store.hpp"
#include "ui/catalog/catalog_helpers.hpp"
#include "ui/common/async_image.hpp"
#include "ui/common/ui_helpers.hpp"
#include "ui/detail/screenshot_viewer.hpp"
#include "ui/detail/torrent_selection.hpp"
#include "ui/downloads/details_activity.hpp"
#include "ui/theme.hpp"

namespace goonies::ui {

// ---------------------------------------------------------------------------
// Install/status button (O5)
// ---------------------------------------------------------------------------
// The primary action button doubles as a live status surface: while a download
// or install is running it keeps its vivid enabled look and dims the
// not-yet-finished remainder (eShop-style progress fill) instead of greying
// out. setProgress(<0) restores the plain button; >=1 leaves it fully filled.
class InstallButton : public brls::Button {
  public:
    void setProgress(float progress) {
        progress_ = progress < 0.0f ? -1.0f : std::min(1.0f, progress);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        // Scrim the pending remainder before the label so the text stays crisp.
        if (progress_ >= 0.0f && progress_ < 1.0f) {
            float radius   = getCornerRadius();
            float doneWidth = width * progress_;
            nvgSave(vg);
            nvgIntersectScissor(vg, x + doneWidth, y, width - doneWidth, height);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, width, height, radius);
            nvgFillColor(vg, theme::panel());
            nvgFill(vg);
            nvgRestore(vg);
        }
        brls::Button::draw(vg, x, y, width, height, style, ctx);
    }

  private:
    float progress_ = -1.0f;
};

// ---------------------------------------------------------------------------
// Full-screen game page (eShop-style detail + one-tap install)
// ---------------------------------------------------------------------------

class GameDetailActivity : public brls::Activity {
public:
    // hashLower + failure ("" clears) recorded back into the catalog; onChange
    // asks the catalog list to re-badge.
    using FailureCallback =
        std::function<void(const std::string&, const std::string&)>;
    using ChangeCallback = std::function<void()>;
    // O12: fired (deferred one frame) when the page is torn down after the
    // B-pop, so the catalog can re-seat scroll + focus on the opening card.
    using CloseCallback = std::function<void()>;

    GameDetailActivity(CatalogEntry entry, std::string lastFailure,
                       DownloadManager* manager, GameMetadataService* metadata,
                       InstalledTitleService* installed, AppSettings* settings,
                       FailureCallback onFailure, ChangeCallback onChange,
                       CloseCallback onClose = nullptr)
        : entry_(std::move(entry)), lastFailure_(std::move(lastFailure)),
          manager_(manager), metadata_(metadata), installed_(installed),
          settings_(settings),
          onFailure_(std::move(onFailure)), onChange_(std::move(onChange)),
          onClose_(std::move(onClose)),
          alive_(std::make_shared<std::atomic<bool>>(true)),
          cancelled_(std::make_shared<std::atomic<bool>>(false)) {
        const GameMetadata* found = metadata_->findByInfoHash(entry_.infoHash);
        presentation_ = resolveCatalogPresentation(entry_, found);
        titleId_ = presentation_.titleId;
        if (titleId_.empty() && !entry_.titleId.empty()) {
            titleId_ = entry_.titleId;
        }
        if (titleId_.empty()) {
            uint64_t parsed = parseTitleIdFromFileName(entry_.title);
            if (parsed != 0) {
                titleId_ = InstalledTitleService::formatTitleId(parsed);
            }
        }

        // F3: eShop-style two-column page. Left column is fixed (cover +
        // install button + size/status); the right column scrolls on its own.
        auto* content = new brls::Box(brls::Axis::ROW);
        content->setPadding(24, 40, 24, 40);
        content->setBackgroundColor(theme::color("brls/background"));
        buildLeftColumn(content);
        buildRightColumn(content);
        content->setDefaultFocusedIndex(0);

        frame_ = new brls::AppletFrame(content);
        frame_->setBackgroundColor(theme::color("brls/background"));
        frame_->setTitle(presentation_.title);
        frame_->setDefaultFocusedIndex(1);
    }

    ~GameDetailActivity() override {
        alive_->store(false);
        cancelled_->store(true);
        timer_.stop();
        if (onChange_)
            onChange_();  // refresh the row badge on the way back
        // O12: deferred a frame — the activity is mid-teardown here and
        // popActivity() has already unwound the focus stack, so the catalog
        // can safely take the focus back now.
        if (onClose_)
            brls::sync([onClose = std::move(onClose_)] { onClose(); });
    }

    bool isTranslucent() override { return false; }

    brls::View* createContentView() override { return frame_; }

    void onContentAvailable() override {
        registerAction(t("Cancelar", "Cancel", "Cancelar"), brls::BUTTON_Y, [this](brls::View*) {
            if (busy_)
                cancelled_->store(true);
            return true;
        });
        refreshButtons();
        timer_.setCallback([this] { refreshButtons(); });
        timer_.start(500);
        if (primary_) {
            brls::Application::giveFocus(primary_);
        }
        brls::sync([this]() {
            if (primary_)
                brls::Application::giveFocus(primary_);
        });
    }

private:
    static constexpr float kLeftColumnWidth = 330.0f;

    // Left column: cover, full-width Install/Options, size, status (F3).
    void buildLeftColumn(brls::Box* content) {
        auto* left = new brls::Box(brls::Axis::COLUMN);
        left->setWidth(kLeftColumnWidth);
        left->setMarginRight(28);
        left->setPadding(16, 16, 16, 16);
        left->setCornerRadius(16.0f);
        left->setBackgroundColor(nvgRGBA(18, 22, 32, 240));
        left->setBorderThickness(1.0f);
        left->setBorderColor(nvgRGBA(255, 255, 255, 12));
        left->setFocusable(false);
        left->setHideHighlightBorder(true);
        left->setHideHighlightBackground(true);

        if (!presentation_.coverUrl.empty()) {
            auto* cover = new AsyncRgbaImage();
            cover->setWidth(kLeftColumnWidth - 32);
            cover->setHeight(240);
            cover->setCornerRadius(12.0f);
            cover->setScalingType(brls::ImageScalingType::FIT);
            cover->setClipsToBounds(false);
            cover->setFocusable(false);
            cover->setHideHighlightBorder(true);
            cover->setHideHighlightBackground(true);
            loadImageInto(cover, metadata_, presentation_.coverUrl);
            left->addView(cover);
        }

        // Status pill under cover (Installed badge / size badge)
        auto* badgeRow = new brls::Box(brls::Axis::ROW);
        badgeRow->setMarginTop(12);
        badgeRow->setAlignItems(brls::AlignItems::CENTER);
        badgeRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

        if (installed_ && installed_->contains(titleId_)) {
            auto* instPill = new brls::Box();
            instPill->setPadding(4, 10, 4, 10);
            instPill->setCornerRadius(6.0f);
            instPill->setBackgroundColor(nvgRGBA(16, 185, 129, 45));
            instPill->setBorderThickness(1.0f);
            instPill->setBorderColor(nvgRGBA(16, 185, 129, 140));

            auto* instLbl = new brls::Label();
            instLbl->setFontSize(12);
            instLbl->setTextColor(nvgRGB(16, 220, 140));
            instLbl->setText(t("✓ INSTALADO", "✓ INSTALLED", "✓ INSTALADO"));
            instPill->addView(instLbl);
            badgeRow->addView(instPill);
        }

        auto* sizePill = new brls::Box();
        sizePill->setPadding(4, 10, 4, 10);
        sizePill->setCornerRadius(6.0f);
        sizePill->setBackgroundColor(nvgRGBA(0, 210, 247, 30));
        sizePill->setBorderThickness(1.0f);
        sizePill->setBorderColor(nvgRGBA(0, 210, 247, 90));

        auto* sizeLbl = new brls::Label();
        sizeLbl->setFontSize(12);
        sizeLbl->setTextColor(theme::neonCyan());
        sizeLbl->setText(entry_.size ? formatBytes(entry_.size) : std::string("Unknown"));
        sizePill->addView(sizeLbl);
        badgeRow->addView(sizePill);

        left->addView(badgeRow);

        primary_ = new InstallButton();
        primary_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        primary_->setFontSize(16);
        primary_->setHeight(58);
        primary_->setCornerRadius(12.0f);
        primary_->setMarginTop(14);
        primary_->setText(t("Instalar Juego", "Install Game", "Instalar Jogo"));
        primary_->setFocusable(true);
        primary_->registerClickAction([this](brls::View*) {
            onPrimary();
            return true;
        });
        left->addView(primary_);

        secondary_ = new brls::Button();
        secondary_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
        secondary_->setFontSize(14);
        secondary_->setHeight(52);
        secondary_->setCornerRadius(12.0f);
        secondary_->setMarginTop(10);
        secondary_->setText(t("Selecciona archivos a descargar", "Select files to download", "Selecionar arquivos para baixar"));
        secondary_->setFocusable(true);
        secondary_->registerClickAction([this](brls::View*) {
            onSecondary();
            return true;
        });
        left->addView(secondary_);

        // Locked vertical navigation between Install and Options
        primary_->setCustomNavigationRoute(brls::FocusDirection::DOWN, secondary_);
        primary_->setCustomNavigationRoute(brls::FocusDirection::UP, primary_);
        primary_->setCustomNavigationRoute(brls::FocusDirection::LEFT, primary_);

        secondary_->setCustomNavigationRoute(brls::FocusDirection::UP, primary_);
        secondary_->setCustomNavigationRoute(brls::FocusDirection::DOWN, secondary_);
        secondary_->setCustomNavigationRoute(brls::FocusDirection::LEFT, secondary_);

        statusLabel_ = new brls::Label();
        statusLabel_->setFontSize(12);
        statusLabel_->setMarginTop(12);
        statusLabel_->setTextColor(theme::neonCyan());
        statusLabel_->setText(t("Instalar anade este juego a tu consola.",
                                "Install adds this game to your console.",
                                "Instalar adiciona este jogo ao seu console."));
        left->addView(statusLabel_);

        left->setDefaultFocusedIndex(presentation_.coverUrl.empty() ? 0 : 1);
        content->addView(left);
    }

    // Right column (scrolls): fact table, screenshots, description. The game
    // title already lives in the AppletFrame header, so do not repeat it here.
    void buildRightColumn(brls::Box* content) {
        auto* right = new brls::Box(brls::Axis::COLUMN);
        right->setPadding(0, 12, 24, 0);
        right->setFocusable(false);
        right->setHideHighlightBorder(true);
        right->setHideHighlightBackground(true);

        buildFactsTable(right);

        const std::vector<std::string>& screenshots = presentation_.screenshots;
        if (!screenshots.empty()) {
            auto* shots = new brls::Label();
            shots->setFontSize(theme::kFontSmall);
            shots->setMarginTop(24);
            shots->setMarginBottom(8);
            shots->setTextColor(theme::textSecondary());
            shots->setText("Screenshots");
            right->addView(shots);

            std::string viewerTitle = presentation_.title;
            auto* rail = new brls::Box(brls::Axis::ROW);
            rail->setHeight(150);
            rail->setFocusable(false);
            rail->setHideHighlightBorder(true);
            rail->setHideHighlightBackground(true);

            // Fit thumbnails cleanly within right column without nesting broken scrolling frames
            size_t maxShots = std::min(screenshots.size(), static_cast<size_t>(3));
            for (size_t i = 0; i < maxShots; ++i) {
                auto* image = new AsyncRgbaImage();
                image->setWidth(250);
                image->setHeight(140);
                image->setMarginRight(12);
                image->setCornerRadius(theme::kRadiusSmall);
                image->setFocusable(true);
                image->setScalingType(brls::ImageScalingType::FIT);
                image->setClipsToBounds(false);  // no letterbox edge bands
                // O6: A opens the fullscreen pager at this shot.
                image->registerClickAction(
                    [this, screenshots, i, viewerTitle](brls::View*) {
                        brls::Application::pushActivity(
                            new ScreenshotViewerActivity(metadata_, screenshots,
                                                         i, viewerTitle));
                        return true;
                    });
                image->setCustomNavigationRoute(brls::FocusDirection::LEFT, primary_);
                loadImageInto(image, metadata_, screenshots[i]);
                rail->addView(image);
            }
            right->addView(rail);
        }

        buildDescription(right);

        std::string warn;
        if (!lastFailure_.empty()) {
            warn = "Last attempt: " + lastFailure_;
        } else {
            std::string health = badgeForCatalogHealth(entry_);
            if (!health.empty() && health != "Fresh") {
                warn = "Catalog health: " + health;
                if (!entry_.healthReason.empty())
                    warn += " (" + entry_.healthReason + ")";
            }
        }
        if (!warn.empty()) {
            auto* warning = new brls::Label();
            warning->setFontSize(theme::kFontCaption);
            warning->setMarginTop(16);
            warning->setTextColor(theme::warning());
            warning->setText(warn);
            right->addView(warning);
        }

        // Raw catalog release title (moved off the list per F2).
        auto* release = new brls::Label();
        release->setFontSize(theme::kFontCaption);
        release->setMarginTop(16);
        release->setTextColor(theme::textTertiary());
        release->setText("Release: " + entry_.title);
        right->addView(release);

        auto* scroll = new brls::ScrollingFrame();
        scroll->setFocusable(false);
        scroll->setHideHighlightBorder(true);
        scroll->setHideHighlightBackground(true);
        scroll->setGrow(1);
        scroll->setContentView(right);
        content->addView(scroll);
    }

    // S4: facts as label/value rows instead of one glued string.
    void buildFactsTable(brls::Box* right) {
        auto* table = new brls::Box(brls::Axis::COLUMN);
        table->setPadding(14, 18, 14, 18);
        table->setCornerRadius(14.0f);
        table->setBackgroundColor(nvgRGBA(18, 22, 32, 235));
        table->setBorderThickness(1.0f);
        table->setBorderColor(nvgRGBA(255, 255, 255, 12));
        table->setFocusable(false);
        table->setHideHighlightBorder(true);
        table->setHideHighlightBackground(true);

        auto* heading = new brls::Label();
        heading->setFontSize(13);
        heading->setTextColor(theme::neonCyan());
        heading->setText(t("FICHA TECNICA", "GAME DETAILS", "DETALHES DO JOGO"));
        heading->setMarginBottom(10);
        table->addView(heading);

        addFactRow(table, t("Desarrollador", "Developer", "Desenvolvedor"), presentation_.developer);
        addFactRow(table, t("Distribuidor", "Publisher", "Distribuidor"), presentation_.publisher);
        addFactRow(table, t("Lanzamiento", "Release", "Lancamento"), presentation_.releaseDate);
        addFactRow(table, t("Genero", "Genre", "Genero"), presentation_.genre);
        addFactRow(table, t("Tamano", "Size", "Tamanho"),
                   entry_.size ? formatBytes(entry_.size)
                               : std::string("Unknown"));
        addFactRow(table, t("ID de Titulo", "Title ID", "ID do Titulo"), titleId_);
        right->addView(table);
    }

    void addFactRow(brls::Box* table, const std::string& name,
                    const std::string& value) {
        if (value.empty())
            return;
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginTop(6);
        row->setFocusable(false);
        row->setHideHighlightBorder(true);
        row->setHideHighlightBackground(true);
        auto* key = new brls::Label();
        key->setFontSize(14);
        key->setTextColor(theme::textTertiary());
        key->setWidth(160);
        key->setText(name);
        row->addView(key);
        auto* val = new brls::Label();
        val->setFontSize(14);
        val->setTextColor(theme::textSecondary());
        val->setGrow(1);
        val->setText(value);
        row->addView(val);
        table->addView(row);
    }

    // S5: reversible "Show more" instead of a hard cut.
    void buildDescription(brls::Box* right) {
        std::string text = presentation_.description;
        if (text.empty()) {
            auto* missing = new brls::Label();
            missing->setFontSize(theme::kFontSmall);
            missing->setMarginTop(24);
            missing->setText(t("Sin descripcion disponible.", "No description available.", "Sem descricao disponivel."));
            right->addView(missing);
            return;
        }

        auto* descCard = new brls::Box(brls::Axis::COLUMN);
        descCard->setMarginTop(18);
        descCard->setPadding(14, 18, 14, 18);
        descCard->setCornerRadius(14.0f);
        descCard->setBackgroundColor(nvgRGBA(18, 22, 32, 235));
        descCard->setBorderThickness(1.0f);
        descCard->setBorderColor(nvgRGBA(255, 255, 255, 12));
        descCard->setFocusable(false);

        auto* heading = new brls::Label();
        heading->setFontSize(13);
        heading->setTextColor(theme::neonCyan());
        heading->setText(t("SINOPSIS", "DESCRIPTION", "SINOPSE"));
        heading->setMarginBottom(10);
        descCard->addView(heading);

        auto* desc = new brls::Label();
        desc->setFontSize(14);
        bool truncated = text.size() > 900;
        desc->setText(truncated ? shortDescription(text) : text);
        descCard->addView(desc);
        if (truncated) {
            auto* more = new brls::Button();
            more->setStyle(&brls::BUTTONSTYLE_BORDERLESS);
            more->setFontSize(14);
            more->setTextColor(theme::neonCyan());
            more->setMarginTop(6);
            more->setText(t("Mostrar mas...", "Show more...", "Mostrar mais..."));
            more->setCustomNavigationRoute(brls::FocusDirection::LEFT, primary_);
            more->registerClickAction([this, desc, more,
                                       text = std::move(text)](brls::View*) {
                desc->setText(text);
                more->setVisibility(brls::Visibility::GONE);
                if (primary_)
                    brls::Application::giveFocus(primary_);
                return true;
            });
            descCard->addView(more);
        }
        right->addView(descCard);
    }

    // Find the managed task for this game, if any.
    const DownloadTask* currentTask() {
        cache_ = manager_->snapshot();
        std::string hash = catalogLower(entry_.infoHash);
        for (const DownloadTask& task : cache_)
            if (catalogLower(task.id) == hash)
                return &task;
        return nullptr;
    }

    static std::string installButtonLabel(const DownloadTask& task) {
        switch (task.status) {
            case DownloadStatus::Queued: return t("En cola", "In queue", "Na fila");
            case DownloadStatus::Checking:
            case DownloadStatus::Downloading: {
                return std::string(t("Descargando ", "Downloading ", "Baixando ")) +
                       std::to_string(percentOf(progressOf(task))) + "%";
            }
            case DownloadStatus::Installing:
            case DownloadStatus::Committing: {
                return std::string(t("Instalando ", "Installing ", "Instalando ")) +
                       std::to_string(percentOf(installProgressOf(task))) + "%";
            }
            case DownloadStatus::Verifying:
                return std::string(t("Verificando ", "Verifying ", "Verificando ")) +
                       std::to_string(percentOf(progressOf(task))) + "%";
            case DownloadStatus::Paused: {
                int pct = percentOf(progressOf(task));
                return pct > 0 ? std::string(t("Pausado ", "Paused ", "Pausado ")) + std::to_string(pct) + "%"
                               : t("Pausado", "Paused", "Pausado");
            }
            case DownloadStatus::Completed:
                return t("Descargado 100%", "Downloaded 100%", "Baixado 100%");
            case DownloadStatus::Installed:
                return t("Instalado 100%", "Installed 100%", "Instalado 100%");
            case DownloadStatus::Error: {
                int pct = percentOf(progressOf(task));
                return pct > 0 ? std::string(t("Error ", "Error ", "Erro ")) + std::to_string(pct) + "%"
                               : t("Error", "Error", "Erro");
            }
            case DownloadStatus::Removing:
                return t("Eliminando", "Removing", "Removendo");
        }
        return t("Instalar Juego", "Install Game", "Instalar Jogo");
    }

    // Fill fraction the status button paints while a task is live: install
    // phases track installed bytes, everything else tracks downloaded bytes.
    static float progressForButton(const DownloadTask& task) {
        switch (task.status) {
            case DownloadStatus::Installing:
            case DownloadStatus::Committing:
                return installProgressOf(task);
            case DownloadStatus::Completed:
            case DownloadStatus::Installed:
                return 1.0f;
            case DownloadStatus::Queued:
                return 0.0f;
            default:
                return progressOf(task);
        }
    }

    // Reflect live task state on the buttons. Skipped while resolving so the
    // inline progress text isn't clobbered.
    void refreshButtons() {
        if (busy_)
            return;
        const DownloadTask* task = currentTask();
        if (task) {
            operationMessage_.clear();
            // O5: the button is the status surface — it stays vivid (enabled)
            // and shows a progress fill instead of greying out. Paused/Error
            // become an actionable "Resume".
            bool actionable = task->status == DownloadStatus::Paused ||
                              task->status == DownloadStatus::Error;
            if (actionable) {
                primary_->setText(t("Reanudar", "Resume", "Retomar"));
                primary_->setProgress(-1.0f);
            } else {
                primary_->setText(installButtonLabel(*task));
                primary_->setProgress(progressForButton(*task));
            }
            primary_->setState(brls::ButtonState::ENABLED);
            secondary_->setText(t("Ver descarga", "View download", "Ver download"));
            secondary_->setState(brls::ButtonState::ENABLED);
            if (task->status == DownloadStatus::Error && !task->error.empty())
                statusLabel_->setText(task->error);
        } else {
            primary_->setText(t("Instalar Juego", "Install Game", "Instalar Jogo"));
            primary_->setProgress(-1.0f);
            primary_->setState(brls::ButtonState::ENABLED);
            secondary_->setText(t("Selecciona archivos a descargar", "Select files to download", "Selecionar arquivos para baixar"));
            secondary_->setState(brls::ButtonState::ENABLED);
            if (!operationMessage_.empty())
                statusLabel_->setText(operationMessage_);
            else if (installed_ && installed_->contains(titleId_))
                statusLabel_->setText(
                    t("Instalado en tu consola. Puedes instalar actualizaciones o DLC.",
                      "Installed on this console. You can still install updates or DLC.",
                      "Instalado no seu console. Voce pode instalar atualizacoes ou DLCs."));
            else
                statusLabel_->setText(
                    t("Instalar anade este juego a tu consola.",
                      "Install adds this game to your console.",
                      "Instalar adiciona este jogo ao seu console."));
        }
    }

    void onPrimary() {
        if (busy_)
            return;
        const DownloadTask* task = currentTask();
        if (task) {
            if (task->status == DownloadStatus::Paused ||
                task->status == DownloadStatus::Error)
                manager_->resume(task->id);
            else
                // O5: tapping the live status button opens the download details.
                brls::Application::pushActivity(
                    new DetailsActivity(task->id, manager_));
            return;
        }
        // One-tap install: resolve, then queue silently (picker only on Options).
        startInstall(false);
    }

    void onSecondary() {
        if (busy_)
            return;
        const DownloadTask* task = currentTask();
        if (task) {
            brls::Application::pushActivity(
                new DetailsActivity(task->id, manager_));
            return;
        }
        // Options: always open the per-file picker after resolve.
        startInstall(true);
    }

    // One-tap: resolve the magnet inline, then import immediately (no second
    // dialog) unless forcePicker is set (the "Options" path), which always
    // opens the per-file selection screen after resolve.
    void startInstall(bool forcePicker) {
        if (busy_)
            return;
        busy_ = true;
        operationMessage_.clear();
        cancelled_->store(false);
        primary_->setState(brls::ButtonState::DISABLED);
        secondary_->setState(brls::ButtonState::DISABLED);
        primary_->setText(t("Resolviendo...", "Resolving...", "Resolvendo..."));
        statusLabel_->setText(t("Buscando fuentes...   (Y para cancelar)", "Finding peers...   (Y to cancel)", "Buscando fontes...   (Y para cancelar)"));

        auto alive = alive_;
        auto cancelled = cancelled_;
        uint32_t serial = gCatalogTempSerial.fetch_add(1);
        if (entry_.isPort) {
            std::string err;
            if (manager_->importPort(entry_.id, entry_.title, entry_.directDownloadUrl, err)) {
                log_msg("[catalog] imported port %s\n", entry_.id.c_str());
                statusLabel_->setText("Added Port to downloads.");
                if (onChange_) onChange_();
            } else {
                log_msg("[catalog] port import failed: %s\n", err.c_str());
                operationMessage_ = err;
                refreshButtons();
                brls::Application::notify(err);
            }
            return;
        }

        std::string tmp = manager_->rootPath() + "/downloads/_catalog_tmp_" +
                          catalogLower(entry_.infoHash) + "_" +
                          std::to_string(serial) + ".torrent";
        std::string magnet = entry_.magnetUri;
        std::vector<uint8_t> infoDict = entry_.infoDict;
        std::string telemetryTag = catalogLower(entry_.infoHash);
        uint64_t startedMs = now_ms();
        brls::async([this, alive, cancelled, magnet, infoDict, tmp,
                     forcePicker, telemetryTag, startedMs] {
            std::string err;
            MagnetResolver resolver;
            auto progress = [this, alive, last = std::string()](
                                const goonies::MagnetProgress& p) mutable {
                std::string text;
                switch (p.stage) {
                    case goonies::MagnetProgress::Stage::FindingPeers:
                        text = "Finding peers...";
                        break;
                    case goonies::MagnetProgress::Stage::Connecting:
                        text = "Contacting peer " +
                               std::to_string(p.peerIndex) + "/" +
                               std::to_string(p.peerCount) + "...";
                        break;
                    case goonies::MagnetProgress::Stage::FetchingMetadata:
                        text = "Fetching metadata " +
                               std::to_string(p.completedPieces) + "/" +
                               std::to_string(p.totalPieces) + "...";
                        break;
                    case goonies::MagnetProgress::Stage::Validating:
                        text = "Validating...";
                        break;
                }
                if (text == last)
                    return;
                last = text;
                brls::sync([this, alive, text] {
                    if (alive->load() && busy_)
                        statusLabel_->setText(text + "   (Y to cancel)");
                });
            };
            std::vector<uint8_t> initialPeers;
            bool ok = resolver.resolveToFile(
                magnet, tmp, *cancelled, progress, err, &initialPeers,
                infoDict.empty() ? nullptr : &infoDict);
            telemetry_log("magnet", telemetryTag.c_str(),
                          "event=resolve ok=%d cancelled=%d duration_ms=%llu "
                          "verified_peers=%u",
                          ok ? 1 : 0, cancelled->load() ? 1 : 0,
                          (unsigned long long)(now_ms() - startedMs),
                          static_cast<unsigned>(initialPeers.size() / 6));
            brls::sync([this, alive, ok, err, tmp, forcePicker,
                        initialPeers = std::move(initialPeers)]() mutable {
                if (!alive->load()) {
                    ::unlink(tmp.c_str());
                    return;
                }
                busy_ = false;
                std::string hash = catalogLower(entry_.infoHash);
                if (!ok) {
                    std::string reason = classifyResolveFailure(err);
                    if (onFailure_)
                        onFailure_(hash, reason);
                    diagnostic_error("magnet", hash.c_str(), "error=%s",
                                     err.c_str());
                    operationMessage_ = reason;
                    refreshButtons();
                    brls::Application::notify(err);
                    ::unlink(tmp.c_str());
                    return;
                }
                if (onFailure_)
                    onFailure_(hash, "");  // clear stale failure
                finishImport(tmp, forcePicker, std::move(initialPeers));
            });
        });
    }

    void finishImport(const std::string& path, bool forcePicker,
                      std::vector<uint8_t> initialPeers) {
        goonies::TorrentPreview preview;
        std::string error;
        if (!DownloadManager::previewTorrent(path, preview, error)) {
            diagnostic_error("catalog", "preview", "error=%s",
                             error.c_str());
            operationMessage_ = error;
            refreshButtons();
            brls::Application::notify(error);
            ::unlink(path.c_str());
            return;
        }

        // Options path: the per-file picker owns the temp file and unlinks it
        // on cancel. Each row chooses Skip, Download, or Install directly.
        if (forcePicker) {
            openSelection(path, std::move(preview), std::move(initialPeers));
            return;
        }

        // One-tap path. No installable packages -> nothing to silently install.
        if (preview.packageCount == 0) {
            operationMessage_ = preview.cartridgeCount > 0
                ? "Cartridge dump (XCI) — open Options to download it."
                : "No installable game files. Open Options to download.";
            refreshButtons();
            brls::Application::notify(operationMessage_);
            ::unlink(path.c_str());
            return;
        }

        // Packages present. Install them silently.
        bool hasBaseGame = false;
        uint64_t appId = 0;
        if (!titleId_.empty()) {
            try { appId = std::stoull(titleId_, nullptr, 16); } catch (...) {}
        }
        if (installed_) {
            if (!titleId_.empty() && installed_->contains(titleId_)) {
                hasBaseGame = true;
            } else if (appId != 0 && installed_->findByApplicationId(appId).has_value()) {
                hasBaseGame = true;
            }
        }
        
        uint32_t extras = preview.fileCount - preview.packageCount;
        std::vector<uint8_t> mask;
        mask.reserve(preview.files.size());
        
        bool anySelected = false;
        size_t skippedPackages = 0;
        
        for (const auto& file : preview.files) {
            if (!file.package) {
                mask.push_back(0);
                continue;
            }
            
            PackageInfo pkg = classifyPackage(file.path, appId);
            bool isInst = installed_ && installed_->isPackageInstalled(pkg);
            
            if (isInst) {
                mask.push_back(0);
                skippedPackages++;
            } else {
                mask.push_back(1);
                anySelected = true;
            }
        }
        
        if (!anySelected) {
            // Check if all packages were skipped because they are already installed
            if (hasBaseGame && skippedPackages > 0) {
                uint32_t installedVer = 0;
                if (installed_) {
                    auto itTitle = installed_->find(titleId_);
                    if (itTitle) installedVer = itTitle->internalVersion;
                }
                // Remember that this torrent holds nothing newer than what is
                // installed, so the game stops being listed as "update available".
                {
                    uint32_t official = NxVersionsService::instance().getLatestVersion(titleId_);
                    if (official == 0) official = installedVer;
                    UpdateDismissStore::instance().dismiss(titleId_, official);
                }
                std::string msg = "Este torrent no contiene ninguna actualizacion nueva.\n\n"
                                  "El contenido del torrent ya esta instalado en tu consola";
                if (installedVer > 0) {
                    msg += " (v" + std::to_string(installedVer / 65536) + " / #" + std::to_string(installedVer) + ")";
                }
                msg += ".\n\nNo se iniciara la descarga para evitar descargas innecesarias.";
                
                auto* dialog = new brls::Dialog(msg);
                dialog->addButton("Ver archivos", [this, path, preview = std::move(preview), peers = std::move(initialPeers)]() mutable {
                    openSelection(path, std::move(preview), std::move(peers));
                });
                dialog->addButton("Aceptar", [path]() {
                    ::unlink(path.c_str());
                });
                dialog->open();
                return;
            }
            openSelection(path, std::move(preview), std::move(initialPeers));
            return;
        }

        std::string id;
        std::string err;
        if (manager_->importTorrent(path, TransferMode::StreamInstall, mask,
                                    id, err, initialPeers, settings_->get().downloadProvider)) {
            log_msg("[catalog] imported torrent %s\n", id.c_str());
            if (extras > 0) {
                statusLabel_->setText(t("Instalando juego. Archivos extra omitidos — usa Selecciona archivos para incluirlos.",
                                        "Installing game files. Extra files skipped — use Select files to include them.",
                                        "Instalando jogo. Arquivos extras ignorados."));
                brls::Application::notify(
                    t("Instalando juego. Archivos extra omitidos.",
                      "Installing game files. Extra files skipped.",
                      "Instalando jogo. Arquivos extras ignorados."));
            } else {
                statusLabel_->setText(t("Anadido. Instalando en la microSD...",
                                        "Added. Installing to microSD...",
                                        "Adicionado. Instalando no microSD..."));
            }
            if (onChange_)
                onChange_();
        } else if (catalogLower(err).find("already in the download manager") !=
                   std::string::npos) {
            statusLabel_->setText(t("Ya esta en descargas.", "Already in downloads.", "Ja esta nos downloads."));
            if (onChange_)
                onChange_();
        } else {
            log_msg("[catalog] import failed from '%s': %s\n",
                    path.c_str(), err.c_str());
            diagnostic_error("catalog", "import", "error=%s", err.c_str());
            operationMessage_ = err;
            refreshButtons();
            brls::Application::notify(err);
        }
        ::unlink(path.c_str());
        refreshButtons();
    }

    void openSelection(const std::string& path,
                       goonies::TorrentPreview preview,
                       std::vector<uint8_t> initialPeers) {
        StreamSelection selection = settings_
            ? settings_->get().streamSelection : StreamSelection::AllFiles;
        uint64_t appId = 0;
        if (!titleId_.empty()) {
            try { appId = std::stoull(titleId_, nullptr, 16); } catch (...) {}
        }
        brls::Application::pushActivity(new TorrentSelectionActivity(
            manager_, path, std::move(preview), TransferMode::StreamInstall,
            selection, std::move(initialPeers), installed_, appId));
    }

    CatalogEntry entry_;
    CatalogPresentation presentation_;
    std::string lastFailure_;
    DownloadManager* manager_;
    GameMetadataService* metadata_;
    InstalledTitleService* installed_;
    AppSettings* settings_;
    std::string titleId_;
    std::string operationMessage_;
    FailureCallback onFailure_;
    ChangeCallback onChange_;
    CloseCallback onClose_;
    std::shared_ptr<std::atomic<bool>> alive_;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    brls::AppletFrame* frame_ = nullptr;
    InstallButton* primary_ = nullptr;
    brls::Button* secondary_ = nullptr;
    brls::Label* statusLabel_ = nullptr;
    brls::RepeatingTimer timer_;
    std::vector<DownloadTask> cache_;
    bool busy_ = false;
};

}  // namespace goonies::ui

