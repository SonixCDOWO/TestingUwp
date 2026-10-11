// DxilProbe - prueba mínima: ¿acepta esta consola shaders DXIL (SM 6.0) además de DXBC (SM 5.1)?
#pragma once
#include <string>
#include <vector>

enum LineLevel { LV_INFO = 0, LV_OK = 1, LV_BAD = 2, LV_WARN = 3, LV_TITLE = 4 };

struct Line {
    std::wstring text;
    int level;
};

struct ProbeEnv {
    std::wstring assets_dir; // carpeta con los .cso (dentro del paquete instalado)
    std::wstring state_dir;  // carpeta escribible (LocalState)
};

/// Ejecuta todas las pruebas. Escribe progreso en state_dir antes de cada paso de riesgo:
/// si el proceso muere dentro de un paso, la siguiente ejecución lo detecta y lo omite.
std::vector<Line> RunProbes(const ProbeEnv& env);
