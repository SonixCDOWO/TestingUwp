#include "probe.h"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace {

// ---------- utilidades de archivo (UTF-8, sin depender de C++/CX) ----------
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

bool ReadBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(size > 0 ? (size_t)size : 0);
    size_t got = out.empty() ? 0 : fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size() && !out.empty();
}

std::wstring ReadText(const std::wstring& path) {
    std::vector<uint8_t> b;
    if (!ReadBytes(path, b)) return {};
    return FromUtf8(std::string(b.begin(), b.end()));
}

// Se abre y se cierra en cada escritura: si el proceso muere después, el dato ya quedó en disco.
void WriteText(const std::wstring& path, const std::wstring& text, bool append) {
    FILE* f = _wfopen(path.c_str(), append ? L"ab" : L"wb");
    if (!f) return;
    std::string s = ToUtf8(text);
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

std::wstring Trim(std::wstring s) {
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == L' ' || s[i] == L'\r' || s[i] == L'\n')) ++i;
    return s.substr(i);
}

std::wstring Hex(uint32_t v) {
    wchar_t buf[16];
    swprintf(buf, 16, L"0x%08X", v);
    return buf;
}

std::wstring Sm(uint32_t v) { // 0x51 -> "5.1"
    wchar_t buf[16];
    swprintf(buf, 16, L"%u.%u", (v >> 4) & 0xF, v & 0xF);
    return buf;
}

// ---------- estado de las pruebas ----------
enum class St { Ok, Fail, Crash, Missing };

struct Runner {
    const ProbeEnv& env;
    std::vector<Line> out;
    std::set<std::wstring> crashed;
    std::map<std::wstring, St> status;

    explicit Runner(const ProbeEnv& e) : env(e) {}

    std::wstring P(const wchar_t* name) const { return env.state_dir + L"\\" + name; }

    void Say(int level, const std::wstring& t) {
        out.push_back({t, level});
        WriteText(P(L"probe_log.txt"), t + L"\r\n", true);
    }

    // Marca el paso en disco ANTES de ejecutarlo. Devuelve false si ya mató al proceso antes.
    bool Enter(const wchar_t* id) {
        if (crashed.count(id)) {
            status[id] = St::Crash;
            Say(LV_BAD, std::wstring(L"[CRASH] ") + id + L": el proceso murió aquí en una ejecución anterior (omitido)");
            return false;
        }
        WriteText(P(L"probe_progress.txt"), id, false);
        return true;
    }
};

bool LoadShader(const ProbeEnv& env, const wchar_t* file, std::vector<uint8_t>& bytes) {
    return ReadBytes(env.assets_dir + L"\\" + file, bytes);
}

HRESULT MakeEmptyRootSig(ID3D12Device* dev, ComPtr<ID3D12RootSignature>& rs) {
    D3D12_ROOT_SIGNATURE_DESC d{};
    d.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) return hr;
    return dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs));
}

void Report(Runner& r, const wchar_t* id, const wchar_t* title, HRESULT hr) {
    if (SUCCEEDED(hr)) {
        r.status[id] = St::Ok;
        r.Say(LV_OK, std::wstring(L"[OK]    ") + title);
    } else {
        r.status[id] = St::Fail;
        r.Say(LV_BAD, std::wstring(L"[FALLA] ") + title + L"  HRESULT=" + Hex((uint32_t)hr));
    }
}

void TestCompute(Runner& r, ID3D12Device* dev, ID3D12RootSignature* rs, const wchar_t* id,
                 const wchar_t* title, const wchar_t* file) {
    if (!r.Enter(id)) return;
    std::vector<uint8_t> cs;
    if (!LoadShader(r.env, file, cs)) {
        r.status[id] = St::Missing;
        r.Say(LV_WARN, std::wstring(L"[?] ") + title + L": falta " + file + L" en el paquete");
        return;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rs;
    d.CS.pShaderBytecode = cs.data();
    d.CS.BytecodeLength = cs.size();
    ComPtr<ID3D12PipelineState> pso;
    Report(r, id, title, dev->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso)));
}

void TestGraphics(Runner& r, ID3D12Device* dev, ID3D12RootSignature* rs, const wchar_t* id,
                  const wchar_t* title, const wchar_t* vs_file, const wchar_t* ps_file) {
    if (!r.Enter(id)) return;
    std::vector<uint8_t> vs, ps;
    if (!LoadShader(r.env, vs_file, vs) || !LoadShader(r.env, ps_file, ps)) {
        r.status[id] = St::Missing;
        r.Say(LV_WARN, std::wstring(L"[?] ") + title + L": faltan archivos .cso en el paquete");
        return;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = rs;
    d.VS.pShaderBytecode = vs.data();
    d.VS.BytecodeLength = vs.size();
    d.PS.pShaderBytecode = ps.data();
    d.PS.BytecodeLength = ps.size();
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = 0xFFFFFFFFu;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.DepthStencilState.DepthEnable = FALSE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso;
    Report(r, id, title, dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)));
}

} // namespace

std::vector<Line> RunProbes(const ProbeEnv& env) {
    Runner r(env);

    // ¿La ejecución anterior murió dentro de un paso? Entonces ese paso se omite ahora.
    std::wstring prev = Trim(ReadText(r.P(L"probe_progress.txt")));
    {
        std::wstringstream ss(ReadText(r.P(L"probe_crashed.txt")));
        std::wstring tok;
        while (ss >> tok) r.crashed.insert(tok);
    }
    if (!prev.empty() && prev != L"DONE" && !r.crashed.count(prev)) {
        r.crashed.insert(prev);
        WriteText(r.P(L"probe_crashed.txt"), prev + L"\r\n", true);
    }
    WriteText(r.P(L"probe_progress.txt"), L"START", false);
    WriteText(r.P(L"probe_log.txt"), L"\r\n==== nueva ejecución ====\r\n", true);

    r.Say(LV_TITLE, L"DxilProbe: ¿acepta esta consola shaders DXIL (SM 6.0)?");
    if (!r.crashed.empty()) {
        std::wstring list;
        for (auto& c : r.crashed) list += c + L" ";
        r.Say(LV_WARN, L"Pasos que ya mataron el proceso antes: " + list);
    }

    // ---- 1. Adaptador ----
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter1> adapter;
    if (!r.Enter(L"dxgi")) return r.out;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        r.Say(LV_BAD, L"CreateDXGIFactory1 falló");
        return r.out;
    }
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (!adapter && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            adapter = a;
            r.Say(LV_INFO, std::wstring(L"Adaptador: ") + d.Description);
        }
    }
    if (!adapter) {
        r.Say(LV_BAD, L"No hay adaptador por hardware");
        return r.out;
    }

    // ---- 2. Dispositivo D3D12 (feature level mínimo 11_0, como Mesa) ----
    ComPtr<ID3D12Device> dev;
    if (!r.Enter(L"device")) {
        r.Say(LV_BAD, L"El proceso murió creando el dispositivo D3D12");
        return r.out;
    }
    HRESULT hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev));
    if (FAILED(hr)) {
        r.Say(LV_BAD, L"D3D12CreateDevice falló: " + Hex((uint32_t)hr));
        return r.out;
    }

    // ---- 3. Capacidades básicas ----
    if (r.Enter(L"caps")) {
        static const D3D_FEATURE_LEVEL lv[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                               D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
        fl.NumFeatureLevels = 4;
        fl.pFeatureLevelsRequested = lv;
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl)))) {
            r.Say(LV_INFO, L"Feature level máximo: " + Hex((uint32_t)fl.MaxSupportedFeatureLevel) +
                               L"  (0xC000 = 12_0)");
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
        if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o)))) {
            r.Say(LV_INFO, L"Resource binding tier: " + std::to_wstring((int)o.ResourceBindingTier));
        }
    }

    // ---- 4. Shader model: se reproduce el bucle de Mesa (d3d12_screen.cpp) ----
    if (r.Enter(L"shader_model")) {
        static const uint32_t req[] = {0x68, 0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x51};
        std::wstring row;
        bool first = true;
        uint32_t mesa_value = 0;
        for (uint32_t q : req) {
            D3D12_FEATURE_DATA_SHADER_MODEL d{};
            d.HighestShaderModel = (D3D_SHADER_MODEL)q;
            HRESULT h = dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &d, sizeof(d));
            if (SUCCEEDED(h)) {
                row += Sm(q) + L"->" + Sm((uint32_t)d.HighestShaderModel) + L"  ";
                if (first && q != 0x51) {
                    first = false;
                    uint32_t v = (uint32_t)d.HighestShaderModel;
                    mesa_value = ((v & 0xf0) << 12) | (v & 0xf);
                }
            } else {
                row += Sm(q) + L":error " + Hex((uint32_t)h) + L"  ";
            }
        }
        r.Say(LV_INFO, L"Shader model (pedido->devuelto): " + row);
        if (first) {
            r.Say(LV_WARN, L"Mesa no obtendría ningún shader model (max_shader_model sin asignar)");
        } else {
            bool ge60 = mesa_value >= 0x60000;
            r.Say(ge60 ? LV_OK : LV_WARN,
                  L"max_shader_model de Mesa = " + Hex(mesa_value) +
                      (ge60 ? L"  (>= 6.0)" : L"  (< 6.0; Mesa no rechaza el dispositivo igualmente)"));
        }
    }

    // ---- 5. Root signature vacía (necesaria para los PSO de prueba) ----
    ComPtr<ID3D12RootSignature> rs;
    if (!r.Enter(L"rootsig")) return r.out;
    hr = MakeEmptyRootSig(dev.Get(), rs);
    if (FAILED(hr)) {
        r.Say(LV_BAD, L"No se pudo crear la root signature: " + Hex((uint32_t)hr));
        return r.out;
    }

    // ---- 6. PSO: primero los de control (DXBC 5.1), después DXIL 6.0 ----
    r.Say(LV_INFO, L"Creación de pipelines (el DXIL es la prueba de riesgo):");
    TestCompute(r, dev.Get(), rs.Get(), L"pso_cs_51", L"Compute  DXBC SM 5.1 (control)", L"cs_5_1.cso");
    TestGraphics(r, dev.Get(), rs.Get(), L"pso_gfx_51", L"Graphics DXBC SM 5.1 (control)", L"vs_5_1.cso", L"ps_5_1.cso");
    TestCompute(r, dev.Get(), rs.Get(), L"pso_cs_60", L"Compute  DXIL SM 6.0", L"cs_6_0.cso");
    TestGraphics(r, dev.Get(), rs.Get(), L"pso_gfx_60", L"Graphics DXIL SM 6.0 (lo que usa Mesa)", L"vs_6_0.cso", L"ps_6_0.cso");

    // ---- 7. Veredicto ----
    auto ok = [&](const wchar_t* id) { return r.status.count(id) && r.status[id] == St::Ok; };
    auto bad = [&](const wchar_t* id) {
        return r.status.count(id) && (r.status[id] == St::Fail || r.status[id] == St::Crash);
    };
    bool control = ok(L"pso_cs_51") && ok(L"pso_gfx_51");
    bool dxil_ok = ok(L"pso_cs_60") && ok(L"pso_gfx_60");
    bool dxil_bad = bad(L"pso_cs_60") || bad(L"pso_gfx_60");

    r.Say(LV_TITLE, L"VEREDICTO");
    if (!control) {
        r.Say(LV_WARN, L"Ni el DXBC 5.1 de control funciona del todo: el problema es más general (mira [FALLA] / [?]).");
    } else if (dxil_ok) {
        r.Say(LV_OK, L"DXIL 6.0 SÍ funciona aquí: el crash de Azahar viene de otra parte, no del formato de shader.");
    } else if (dxil_bad) {
        r.Say(LV_BAD, L"DXBC 5.1 funciona y DXIL 6.0 NO: Mesa (que genera DXIL) no puede dibujar en esta consola.");
        r.Say(LV_BAD, L"Un backend propio tendría que usar DXBC (SM 5.1).");
    } else {
        r.Say(LV_WARN, L"Resultado incompleto: faltan archivos o pasos omitidos.");
    }
    r.Say(LV_INFO, L"Log completo: LocalState\\probe_log.txt (Device Portal > File explorer)");
    if (!r.crashed.empty()) {
        r.Say(LV_INFO, L"Si la app se cerró sola antes, es normal: ábrela de nuevo para ver el resultado.");
    }

    WriteText(r.P(L"probe_progress.txt"), L"DONE", false);
    return r.out;
}
