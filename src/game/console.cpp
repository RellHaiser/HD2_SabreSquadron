#include "game/console.h"

#include <cstring>

#include "game/pending.h"

namespace {

const char INPUT_PROMPT[] = ">";                         // 0x0083686c

char *NewInputBuffer()
{
    char *buffer = new char[C_Console::INPUT_BUFFER_SIZE];
    std::strcpy(buffer, INPUT_PROMPT);
    return buffer;
}

} // namespace

C_Console::C_Console()
{
    InitializeCriticalSection(&m_lock);
    m_input = NewInputBuffer();
}

C_Console::~C_Console()
{
    DeleteCriticalSection(&m_lock);
    delete[] m_input;
    for (char *line : m_history)
        delete[] line;
    for (char *command : m_commands)
        delete[] command;
    while (!m_widgets.empty()) {
        delete m_widgets.back();
        m_widgets.pop_back();
    }
}

void C_Console::Create()
{
    WNDCLASSEXA windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = ConsoleWindowProc;
    windowClass.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH));
    windowClass.lpszClassName = "WndConsole";
    windowClass.hIconSm = LoadIconA(nullptr, IDI_APPLICATION);
    RegisterClassExA(&windowClass);

    m_window = CreateWindowExA(0, "WndConsole", "Hidden & Dangerous II Console", 0x10EE000D,
                               0, 0, 480, 360, nullptr, nullptr, nullptr, nullptr);
    ShowWindow(m_window, SW_SHOW);

    // 10 point bold Courier.
    HDC dc = GetDC(m_window);
    const int height = MulDiv(10, GetDeviceCaps(dc, LOGPIXELSY), 72);
    m_font = CreateFontA(-height, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "Courier");
    ReleaseDC(m_window, dc);
}

char *C_Console::PopCommand()
{
    if (m_commands.empty())
        return nullptr;
    char *command = m_commands.back();
    m_commands.pop_back();
    return command;
}

void C_Console::OnChar(char ch)
{
    if (ch == '\b') {
        const size_t length = std::strlen(m_input);
        if (length > 0)
            m_input[length - 1] = '\0';
        RedrawInputLine();
    } else if (ch == '\r') {
        SubmitInputLine();
    } else if (std::strlen(m_input) < MAX_INPUT_LENGTH) {
        std::strncat(m_input, &ch, 1);
        RedrawInputLine();
    }
}

void C_Console::AddLine(const char *text)
{
    char *line = new char[std::strlen(text) + 1];
    std::strcpy(line, text);

    EnterCriticalSection(&m_lock);
    m_history.push_front(line);
    if (m_history.size() > MAX_HISTORY_LINES) {
        delete[] m_history.back();
        m_history.pop_back();
    }
    LeaveCriticalSection(&m_lock);
    InvalidateRect(m_window, nullptr, TRUE);
}

void C_Console::MoveInputLineToHistory()
{
    EnterCriticalSection(&m_lock);
    m_history.push_front(m_input);
    if (m_history.size() > MAX_HISTORY_LINES) {
        delete[] m_history.back();
        m_history.pop_back();
    }
    LeaveCriticalSection(&m_lock);
    m_input = NewInputBuffer();
    InvalidateRect(m_window, nullptr, TRUE);
}

void C_Console::SubmitInputLine()
{
    // The queued command keeps the prompt character in front.
    m_commands.push_front(m_input);
    AddLine(m_input);
    m_input = NewInputBuffer();
}

RECT C_Console::InputLineRect(const RECT &client) const
{
    RECT rect = client;
    rect.top = rect.bottom * 2 - m_scrollPos - 17;
    return rect;
}

void C_Console::RedrawInputLine()
{
    HDC dc = GetDC(m_window);
    SelectObject(dc, m_font);
    RECT rect = InputLineRect(m_client);

    // Pad with spaces so the previous, longer text is overwritten.
    char text[INPUT_BUFFER_SIZE];
    std::strcpy(text, m_input);
    const size_t length = std::strlen(text);
    std::memset(text + length, ' ', INPUT_BUFFER_SIZE - 1 - length);
    text[INPUT_BUFFER_SIZE - 1] = '\0';

    SetBkColor(dc, RGB(0xC0, 0xC0, 0xC0));
    DrawTextA(dc, text, -1, &rect, 0);
    for (C_ConsoleWidget *widget : m_widgets)
        widget->Draw(dc);
    ReleaseDC(m_window, dc);
}

void C_Console::Paint(HDC dc, const RECT &client)
{
    m_client = client;
    SelectObject(dc, m_font);
    SetBkMode(dc, TRANSPARENT);

    RECT rect = InputLineRect(client);
    DrawTextA(dc, m_input, -1, &rect, 0);

    // History upwards from the input line, newest first, 12 pixels per line.
    rect.top -= 5;
    EnterCriticalSection(&m_lock);
    for (const char *line : m_history) {
        rect.top -= 12;
        DrawTextA(dc, line, -1, &rect, 0);
    }
    LeaveCriticalSection(&m_lock);
}

LRESULT CALLBACK ConsoleWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        return 0;
    case WM_SIZE: {
        const int height = HIWORD(lParam);
        SetScrollRange(window, SB_VERT, 0, height, FALSE);
        GetConsole()->SetScrollPosition(height);
        SetScrollPos(window, SB_VERT, height, TRUE);
        return DefWindowProcA(window, WM_SIZE, wParam, lParam);
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &paint.rcPaint);
        GetConsole()->Paint(dc, paint.rcPaint);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        // Closing the console quits the game.
        GetGameApp()->m_state->SetState(GAME_STATE_QUIT);
        return 0;
    case WM_CHAR:
        GetConsole()->OnChar(static_cast<char>(wParam));
        return 0;
    case WM_VSCROLL:
        if (LOWORD(wParam) == SB_THUMBPOSITION || LOWORD(wParam) == SB_THUMBTRACK) {
            const int pos = static_cast<short>(HIWORD(wParam));
            SetScrollPos(window, SB_VERT, pos, TRUE);
            GetConsole()->SetScrollPosition(pos);
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;
    default:
        return DefWindowProcA(window, message, wParam, lParam);
    }
}
