// DxilProbe - app UWP mínima (C++/WinRT). Ejecuta las pruebas de probe.cpp y muestra el resultado
// en pantalla con Direct2D/DirectWrite. También escribe todo en LocalState\probe_log.txt.
#define NOMINMAX
#include <windows.h>
#include <unknwn.h> // antes de los headers de winrt (interop COM)
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <dwrite.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <vector>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Core.h>

#include "probe.h"

// Se califica todo con winrt:: (wrl/client.h declara un ::Windows parcial que causaría ambigüedad).
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::UI::Core;
using Microsoft::WRL::ComPtr;

namespace {

// Dibujo de texto sobre el CoreWindow (D3D11 + Direct2D + DirectWrite)
struct Gfx {
    ComPtr<ID3D11Device> d3d;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID2D1Factory1> d2d;
    ComPtr<ID2D1Device> d2d_dev;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<IDWriteFactory> dw;
    ComPtr<IDWriteTextFormat> fmt, title_fmt;
    ComPtr<ID2D1SolidColorBrush> b_info, b_ok, b_bad, b_warn, b_title;
    float w = 0, h = 0, line_h = 24;

    bool Init(IUnknown* window, UINT wpx, UINT hpx) {
        w = (float)wpx;
        h = (float)hpx;

        const D3D_FEATURE_LEVEL fls[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                         D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        ComPtr<ID3D11DeviceContext> ctx;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, fls, (UINT)_countof(fls),
                                     D3D11_SDK_VERSION, &d3d, nullptr, &ctx)))
            return false;

        ComPtr<IDXGIDevice1> dxgi_dev;
        if (FAILED(d3d.As(&dxgi_dev))) return false;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgi_dev->GetAdapter(&adapter))) return false;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = wpx;
        sd.Height = hpx;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        if (FAILED(factory->CreateSwapChainForCoreWindow(d3d.Get(), window, &sd, nullptr, &swap)))
            return false;

        D2D1_FACTORY_OPTIONS fo = {};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                     reinterpret_cast<void**>(d2d.GetAddressOf()))))
            return false;
        if (FAILED(d2d->CreateDevice(dxgi_dev.Get(), &d2d_dev))) return false;
        if (FAILED(d2d_dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc))) return false;

        ComPtr<IDXGISurface> surf;
        if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&surf)))) return false;
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
        if (FAILED(dc->CreateBitmapFromDxgiSurface(surf.Get(), &bp, &target))) return false;
        dc->SetTarget(target.Get());

        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(dw.GetAddressOf()))))
            return false;
        const float fs = std::max(14.0f, h / 54.0f); // ~20 px a 1080p
        line_h = fs * 1.35f;
        // Consolas si existe; si no, Segoe UI (la consola puede no traer Consolas)
        const wchar_t* families[] = {L"Consolas", L"Segoe UI"};
        bool fonts_ok = false;
        for (const wchar_t* fam : families) {
            fmt.Reset();
            title_fmt.Reset();
            if (SUCCEEDED(dw->CreateTextFormat(fam, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               fs, L"en-us", &fmt)) &&
                SUCCEEDED(dw->CreateTextFormat(fam, nullptr, DWRITE_FONT_WEIGHT_BOLD,
                                               DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                               fs * 1.2f, L"en-us", &title_fmt))) {
                fonts_ok = true;
                break;
            }
        }
        if (!fonts_ok) return false;
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        title_fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        dc->CreateSolidColorBrush(D2D1::ColorF(0.86f, 0.88f, 0.92f), &b_info);
        dc->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.92f, 0.45f), &b_ok);
        dc->CreateSolidColorBrush(D2D1::ColorF(1.00f, 0.38f, 0.33f), &b_bad);
        dc->CreateSolidColorBrush(D2D1::ColorF(1.00f, 0.80f, 0.28f), &b_warn);
        dc->CreateSolidColorBrush(D2D1::ColorF(0.45f, 0.82f, 1.00f), &b_title);
        return true;
    }

    void Draw(const std::vector<Line>& lines) {
        dc->BeginDraw();
        dc->Clear(D2D1::ColorF(0.04f, 0.05f, 0.08f));
        const float x = w * 0.05f; // margen de zona segura de TV
        float y = h * 0.06f;
        for (const Line& l : lines) {
            ID2D1SolidColorBrush* br = b_info.Get();
            switch (l.level) {
            case LV_OK: br = b_ok.Get(); break;
            case LV_BAD: br = b_bad.Get(); break;
            case LV_WARN: br = b_warn.Get(); break;
            case LV_TITLE: br = b_title.Get(); break;
            default: break;
            }
            IDWriteTextFormat* f = (l.level == LV_TITLE) ? title_fmt.Get() : fmt.Get();
            if (l.level == LV_TITLE) y += line_h * 0.3f;
            dc->DrawText(l.text.c_str(), (UINT32)l.text.size(), f,
                         D2D1::RectF(x, y, w - x, y + line_h * 1.5f), br);
            y += line_h;
        }
        dc->EndDraw();
        swap->Present(1, 0);
    }
};

struct App : winrt::implements<App, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() { return *this; }

    void Initialize(CoreApplicationView const& view) {
        view.Activated([](auto&&, auto&&) { CoreWindow::GetForCurrentThread().Activate(); });
    }

    void SetWindow(CoreWindow const& window) {
        m_window = window;
        window.Closed([this](auto&&, auto&&) { m_closed = true; });
    }

    void Load(winrt::hstring const&) {}
    void Uninitialize() {}

    void Run() {
        m_window.Activate();
        m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

        const double scale =
            winrt::Windows::Graphics::Display::DisplayInformation::GetForCurrentView().RawPixelsPerViewPixel();
        const auto bounds = m_window.Bounds();
        const UINT wpx = (UINT)(bounds.Width * scale);
        const UINT hpx = (UINT)(bounds.Height * scale);

        Gfx gfx;
        const bool gfx_ok = gfx.Init(winrt::get_unknown(m_window), wpx, hpx);

        std::vector<Line> lines;
        lines.push_back({L"DxilProbe: ejecutando pruebas...", LV_TITLE});
        if (gfx_ok) gfx.Draw(lines);

        ProbeEnv env;
        env.assets_dir =
            std::wstring(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str()) +
            L"\\Assets";
        env.state_dir = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str());
        lines = RunProbes(env);
        if (!gfx_ok) lines.push_back({L"(no se pudo iniciar el dibujo en pantalla; mira probe_log.txt)", LV_WARN});

        while (!m_closed) {
            m_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            if (gfx_ok) gfx.Draw(lines);
            else Sleep(100);
        }
    }

private:
    CoreWindow m_window{nullptr};
    bool m_closed = false;
};

} // namespace

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoreApplication::Run(winrt::make<App>());
    return 0;
}
