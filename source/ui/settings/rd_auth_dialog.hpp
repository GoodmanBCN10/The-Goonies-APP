#pragma once
#include <borealis.hpp>
#include <qrcodegen.hpp>
#include "RealDebridProvider.hpp"

namespace pipensx::ui {

class QRCodeView : public brls::View {
private:
    qrcodegen::QrCode qr;
    int cellSize;
public:
    QRCodeView(const qrcodegen::QrCode& q, int cellSz) : qr(q), cellSize(cellSz) {
        this->setWidth(qr.getSize() * cellSize);
        this->setHeight(qr.getSize() * cellSize);
    }
    
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override {
        int size = qr.getSize();
        
        // Background
        nvgBeginPath(vg);
        nvgRect(vg, x, y, size * cellSize, size * cellSize);
        nvgFillColor(vg, nvgRGB(255, 255, 255));
        nvgFill(vg);

        // Modules
        nvgBeginPath(vg);
        for (int qy = 0; qy < size; qy++) {
            for (int qx = 0; qx < size; qx++) {
                if (qr.getModule(qx, qy)) {
                    nvgRect(vg, x + qx * cellSize, y + qy * cellSize, cellSize, cellSize);
                }
            }
        }
        nvgFillColor(vg, nvgRGB(0, 0, 0));
        nvgFill(vg);
    }
};

inline void ShowRealDebridAuthDialog() {
    static RealDebridProvider* rdProvider = new RealDebridProvider();
    
    if (!rdProvider->StartOAuthDeviceFlow()) {
        brls::Application::notify("Error al iniciar el auth flow con Real-Debrid.");
        return;
    }
    
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::CENTER);
    content->setJustifyContent(brls::JustifyContent::CENTER);
    
    auto* lbl = new brls::Label();
    lbl->setText("Escanea el QR para vincular Real-Debrid:");
    lbl->setFontSize(24);
    lbl->setMarginBottom(20);
    content->addView(lbl);
    
    // Generar QR
    std::string url_str = rdProvider->GetVerificationUrl();
    qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url_str.c_str(), qrcodegen::QrCode::Ecc::LOW);
    
    int size = qr.getSize();
    int cellSize = 6;
    
    auto* qrBox = new brls::Box(brls::Axis::COLUMN);
    qrBox->setBackgroundColor(brls::Theme::getLightTheme().getColor("brls/background"));
    qrBox->setPadding(20, 20, 20, 20);
    qrBox->setAlignItems(brls::AlignItems::CENTER);
    
    auto* qrView = new QRCodeView(qr, cellSize);
    qrBox->addView(qrView);
    content->addView(qrBox);
    
    auto* codeLbl = new brls::Label();
    codeLbl->setText("O introduce este código en la web:\n" + rdProvider->GetUserCode());
    codeLbl->setFontSize(22);
    codeLbl->setMarginTop(10);
    codeLbl->setMarginBottom(40);
    codeLbl->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    content->addView(codeLbl);
    
    auto* dialog = new brls::Dialog(content);
    dialog->addButton("Aceptar", [rdProvider]() {
        if (rdProvider->IsAuthenticated()) {
            brls::Application::notify("Cuenta de Real-Debrid vinculada con éxito.");
            brls::Application::popActivity(); // Pop settings view to force refresh
        } else {
            brls::Application::notify("Validación pendiente. Termina el proceso en la web.");
        }
    });
    dialog->addButton("Cancelar", []() {});
    dialog->open();
}

}
