#include <Windows.h>
#include <winhttp.h>
#include <wchar.h>
#include <time.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// Texto que se muestra en pantalla (protegido con mutex para acceso seguro)
std::wstring g_texto = L"Cargando...";
std::wstring g_weatherLine = L"Cargando clima...";

HFONT g_fontRoboto = nullptr;
int windowLength = 316;
int windowHeight = 196;

// Mutex para sincronizar acceso a g_texto y g_weatherLine entre hilos
std::mutex g_mutex;

// Para controlar la ventana redondeada
void RoundWindow(HWND hwnd)
{
  HRGN region = CreateRoundRectRgn(0, 0, windowLength, windowHeight, 20, 20);
  SetWindowRgn(hwnd, region, TRUE);
  DeleteObject(region);
}

POINT lastMousePos;
bool moving = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
  case WM_SIZE:
    RoundWindow(hwnd);
    break;

  case WM_CREATE:
  {
    SetTimer(hwnd, 1, 10, nullptr);
    RoundWindow(hwnd);
    g_fontRoboto = CreateFontW(
        -16, 0, 0, 0,
        FW_BOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        VARIABLE_PITCH,
        L"Roboto");
    break;
  }

  case WM_TIMER:
  {
    bool ctrlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;

    if (ctrlPressed && altPressed)
    {
      POINT currentPos;
      GetCursorPos(&currentPos);
      if (!moving)
      {
        moving = true;
        lastMousePos = currentPos;
      }
      else
      {
        int dx = currentPos.x - lastMousePos.x;
        int dy = currentPos.y - lastMousePos.y;
        RECT rect;
        GetWindowRect(hwnd, &rect);
        SetWindowPos(hwnd, nullptr,
                     rect.left + dx,
                     rect.top + dy,
                     0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        lastMousePos = currentPos;
      }
    }
    else
    {
      moving = false;
    }
    break;
  }

  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, g_fontRoboto);

    // Acceder al texto protegido por mutex
    std::wstring textoLocal;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      textoLocal = g_texto;
    }

    int y = 20;
    size_t start = 0;
    size_t line = 0;

    // Dibujar líneas con colores según el tipo (clima o reloj)
    while (true)
    {
      size_t pos = textoLocal.find(L'\n', start);
      std::wstring lineText = (pos == std::wstring::npos) ? textoLocal.substr(start) : textoLocal.substr(start, pos - start);

      if (line == 0)
        SetTextColor(hdc, RGB(255, 215, 0)); // Amarillo para clima
      else
        SetTextColor(hdc, RGB(173, 216, 230)); // Azul claro para relojes

      TextOutW(hdc, 20, y, lineText.c_str(), (int)lineText.length());
      y += 22;

      if (pos == std::wstring::npos)
        break;

      start = pos + 1;
      line++;
    }

    SelectObject(hdc, oldFont);
    EndPaint(hwnd, &ps);
    return 0;
  }

  case WM_DESTROY:
    if (g_fontRoboto)
      DeleteObject(g_fontRoboto);
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Actualiza el texto del clima llamando a la API (asegura uso seguro de memoria y actualiza global con mutex)
void UpdateWeather()
{
  const wchar_t *host = L"api.openweathermap.org";
  const wchar_t *path = L"/data/2.5/weather?q=Tijuana&appid={{API_KEY}}&units=metric";

  HINTERNET hSession = WinHttpOpen(L"WeatherApp/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
  if (!hSession)
    return;

  HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTP_PORT, 0);
  if (!hConnect)
  {
    WinHttpCloseHandle(hSession);
    return;
  }

  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
                                          nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

  if (hRequest)
  {
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr))
    {
      DWORD dwSize = 0;
      std::string responseStr;

      do
      {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
          break;

        if (dwSize == 0)
          break;

        std::vector<char> buffer(dwSize + 1);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &bytesRead))
          break;

        buffer[bytesRead] = '\0';
        responseStr += buffer.data();

      } while (dwSize > 0);

      // Buscar temperatura en el JSON (búsqueda simple)
      const char *tempKey = "\"temp\":";
      auto pos = responseStr.find(tempKey);
      if (pos != std::string::npos)
      {
        float tempC = atof(responseStr.c_str() + pos + strlen(tempKey));
        std::lock_guard<std::mutex> lock(g_mutex);
        wchar_t buffer[64];
        swprintf(buffer, 64, L"Clima en Tijuana: %.0f°C", tempC);
        g_weatherLine = buffer;
      }
    }

    WinHttpCloseHandle(hRequest);
  }

  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
}

// Actualiza las horas locales para las zonas definidas y construye el texto completo con clima + relojes
void UpdateClocks()
{
  struct Zone
  {
    const wchar_t *name;
    int offsetHours; // offset UTC sin DST
  };

  static const std::vector<Zone> zones = {
      {L"CDMX, México", -6},
      {L"Madrid, España", 2},
      {L"Bogotá, Colombia", -5},
      {L"Buenos Aires, Argentina", -3},
      {L"Santiago, Chile", -4},
  };

  SYSTEMTIME utcTime;
  GetSystemTime(&utcTime);

  std::wstring result;

  // Primera línea: clima
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    result = g_weatherLine + L"\n\n";
  }

  for (size_t i = 0; i < zones.size(); ++i)
  {
    int hour = utcTime.wHour + zones[i].offsetHours;
    if (hour < 0)
      hour += 24;
    else if (hour >= 24)
      hour -= 24;

    int hour12 = hour % 12;
    if (hour12 == 0)
      hour12 = 12;

    const wchar_t *ampm = (hour < 12) ? L"AM" : L"PM";

    wchar_t buffer[128];
    swprintf(buffer, 128, L"%ls: %02d:%02d:%02d %ls", zones[i].name, hour12, utcTime.wMinute, utcTime.wSecond, ampm);

    result += buffer;
    if (i < zones.size() - 1)
      result += L"\n";
  }

  // Actualizar texto global protegido por mutex
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_texto = result;
  }
}

// Crear ventana con configuración para overlay y transparencia
HWND createWindow()
{
  const wchar_t *className = L"OverlayWindowClass";

  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = className;
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
      className,
      L"Dina",
      WS_POPUP,
      100, 100, windowLength, windowHeight,
      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

  SetLayeredWindowAttributes(hwnd, 0, 200, LWA_ALPHA);
  return hwnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    HWND window = createWindow();
    if (window == nullptr)
    {
        return 1;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    RoundWindow(window);

    CreateThread(nullptr, 0, [](LPVOID param) -> DWORD
                 {
                     HWND hwnd = (HWND)param;
                     while (true)
                     {
                         UpdateWeather();
                         Sleep(25000);
                     }
                     return 0;
                 }, window, 0, nullptr);

    CreateThread(nullptr, 0, [](LPVOID param) -> DWORD
                 {
                     HWND hwnd = (HWND)param;
                     while (true)
                     {
                         UpdateClocks();
                         InvalidateRect(hwnd, nullptr, TRUE);
                         Sleep(1000);
                     }
                     return 0;
                 }, window, 0, nullptr);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
