# DxilProbe

Prueba mínima para tu **Xbox One (modo game)**: ¿acepta la consola shaders **DXIL (SM 6.0)**,
que es lo que genera Mesa, o solo **DXBC (SM 5.1)**?

## Qué hace la app

1. Reproduce el bucle de Mesa (`d3d12_screen.cpp`) que consulta el shader model y muestra qué valor
   guardaría Mesa.
2. Crea 4 pipelines de prueba y muestra el resultado de cada uno:

| Prueba | Formato | Papel |
|---|---|---|
| Compute y Graphics | DXBC SM 5.1 (fxc) | **Control**: debe funcionar |
| Compute y Graphics | DXIL SM 6.0 (dxc, firmado) | **La prueba**: lo que usa Mesa |

3. Si el proceso **muere** dentro de un paso (como haría Mesa con `_Exit`), la app lo anota en disco.
   Al abrirla otra vez te dice en qué paso murió y lo omite para seguir con el resto.

## Cómo compilarla (sin necesitar Windows 10 en tu PC)

1. Crea un repo en GitHub y sube **todo** el contenido de esta carpeta (incluida `.github/`).
2. Pestaña **Actions** → `build-dxilprobe` → **Run workflow**.
3. Al terminar, descarga el artefacto `DxilProbe`: trae `DxilProbe_1.0.0.0.msix` y un `.cer`.

Si prefieres compilar local (Windows 10/11 con Visual Studio 2022 y la carga de trabajo UWP):

(Las comillas en los `-D...` son necesarias en PowerShell: sin ellas parte `10.0` en `10` y `.0`.)

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_SYSTEM_NAME=WindowsStore" "-DCMAKE_SYSTEM_VERSION=10.0"
cmake --build build --config Release
powershell -ExecutionPolicy Bypass -File package.ps1 -BuildDir build
```

## Instalarla en la Xbox

Igual que instalaste Azahar-UWP (Device Portal → Add). El manifiesto usa la misma dependencia de
`Microsoft.VCLibs.140.00` que Azahar, así que debería estar ya instalada. Si Device Portal pide el
certificado, sube el `.cer`.

## Cómo leer el resultado

| Veredicto en pantalla | Significa |
|---|---|
| DXBC 5.1 funciona y DXIL 6.0 **NO** | Mesa/DXIL no puede dibujar en tu consola. Un backend propio debe usar DXBC (SM 5.1) |
| DXIL 6.0 **SÍ** funciona | El crash de Azahar viene de otra parte, no del formato de shader |
| Falla también el control 5.1 | Problema más general (mira los `[FALLA]` / `[?]`) |
| `[CRASH] pso_...` | Ese paso mató el proceso (mismo efecto que un fallo de Mesa) |

Si la app se cierra sola la primera vez: **ábrela de nuevo**. En la segunda ejecución verás el resultado.

El log completo queda en `LocalState\probe_log.txt` (Device Portal → File explorer). Para repetir
la prueba desde cero, desinstala y reinstala la app (se borra `LocalState`).

## Qué está verificado y qué no

- ✅ `probe.cpp` y la parte de dibujo de `main.cpp` pasan una comprobación de sintaxis con mingw-w64.
- ❌ **No he compilado ni ejecutado** el proyecto completo: la parte C++/CX, el CMake para
  `WindowsStore`, el empaquetado y el workflow no se han probado. Es posible que haya que ajustar algo.
- ❓ El runner `windows-2022` de GitHub debería traer la carga de trabajo UWP; si el paso
  *Configurar* se queja de que falta, hay que instalarla en el workflow.
- ❓ El empaquetado copia el método de `dist/uwp/package.ps1` de Azahar-UWP, pero con otro Publisher
  (`CN=DxilProbe`): si tu consola necesita confiar en el certificado, instala el `.cer` igual que hiciste con el de Azahar.
