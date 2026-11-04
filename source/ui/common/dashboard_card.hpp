#pragma once

#include <borealis.hpp>
#include "ui/theme.hpp"
#include <functional>
#include <string>

namespace goonies::ui {

class DashboardCard : public brls::Box {
public:
    DashboardCard(const std::string& title,
                  const std::string& description,
                  const std::string& badgeText,
                  NVGcolor accentColor,
                  std::function<void()> onClick)
        : brls::Box(brls::Axis::COLUMN),
          titleText_(title),
          descText_(description),
          badgeText_(badgeText),
          accentColor_(accentColor),
          onClick_(std::move(onClick)) {

        this->setFocusable(true);
        this->setCornerRadius(16.0f);
        this->setHighlightCornerRadius(16.0f);
        this->setHideHighlightBorder(true);
        this->setHideHighlightBackground(true);

        this->setWidth(380);
        this->setHeight(238);
        this->setPadding(20, 22, 20, 26);
        this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

        // --- Top Bar: Pill Tag & Status ---
        brls::Box* topBox = new brls::Box(brls::Axis::ROW);
        topBox->setAlignItems(brls::AlignItems::CENTER);
        topBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

        if (!badgeText_.empty()) {
            badgeLabel_ = new brls::Label();
            badgeLabel_->setText(badgeText_);
            badgeLabel_->setFontSize(14);
            badgeLabel_->setTextColor(accentColor_);
            topBox->addView(badgeLabel_);
        } else {
            topBox->addView(new brls::Box());
        }

        // Action icon hint / chevron
        actionLabel_ = new brls::Label();
        actionLabel_->setText("[ Entrar > ]");
        actionLabel_->setFontSize(14);
        actionLabel_->setTextColor(nvgRGBA(255, 255, 255, 100));
        topBox->addView(actionLabel_);

        this->addView(topBox);

        // --- Middle: Title & Description ---
        brls::Box* contentBox = new brls::Box(brls::Axis::COLUMN);
        contentBox->setAlignItems(brls::AlignItems::FLEX_START);

        titleLabel_ = new brls::Label();
        titleLabel_->setText(titleText_);
        titleLabel_->setFontSize(26);
        titleLabel_->setMarginBottom(8);
        titleLabel_->setTextColor(theme::textPrimary());
        contentBox->addView(titleLabel_);

        descLabel_ = new brls::Label();
        descLabel_->setText(descText_);
        descLabel_->setFontSize(15);
        descLabel_->setTextColor(theme::textSecondary());
        contentBox->addView(descLabel_);

        this->addView(contentBox);

        // --- Bottom bar spacer ---
        brls::Box* bottomSpacer = new brls::Box();
        bottomSpacer->setHeight(6);
        this->addView(bottomSpacer);

        // Click action
        this->registerClickAction([this](brls::View* view) {
            if (onClick_) onClick_();
            return true;
        });
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override {
        bool focused = this->isFocused();
        float r = 16.0f;

        // 1. Focused glow effect using box gradient
        if (focused) {
            NVGpaint glow = nvgBoxGradient(vg, x, y, width, height, r, 14.0f,
                                           nvgRGBAf(accentColor_.r, accentColor_.g, accentColor_.b, 0.45f),
                                           nvgRGBA(0, 0, 0, 0));
            nvgBeginPath(vg);
            nvgRect(vg, x - 10, y - 10, width + 20, height + 20);
            nvgFillPaint(vg, glow);
            nvgFill(vg);
        }

        // 2. Card body background
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, r);
        nvgFillColor(vg, focused ? theme::cardBgFocus() : theme::cardBg());
        nvgFill(vg);

        // 3. Card border
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, width, height, r);
        if (focused) {
            nvgStrokeWidth(vg, 2.5f);
            nvgStrokeColor(vg, accentColor_);
        } else {
            nvgStrokeWidth(vg, 1.0f);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 22));
        }
        nvgStroke(vg);

        // 4. Left accent indicator pillar
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 5.0f, y + 18.0f, 5.0f, height - 36.0f, 2.5f);
        nvgFillColor(vg, accentColor_);
        nvgFill(vg);

        // Dynamic update of label focus colors
        if (titleLabel_) {
            titleLabel_->setTextColor(focused ? accentColor_ : theme::textPrimary());
        }
        if (actionLabel_) {
            actionLabel_->setTextColor(focused ? accentColor_ : nvgRGBA(255, 255, 255, 110));
        }

        brls::Box::draw(vg, x, y, width, height, style, ctx);
    }

private:
    std::string titleText_;
    std::string descText_;
    std::string badgeText_;
    NVGcolor accentColor_;
    std::function<void()> onClick_;

    brls::Label* titleLabel_ = nullptr;
    brls::Label* descLabel_ = nullptr;
    brls::Label* badgeLabel_ = nullptr;
    brls::Label* actionLabel_ = nullptr;
};

} // namespace goonies::ui
