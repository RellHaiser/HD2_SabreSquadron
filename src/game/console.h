// "Hidden & Dangerous II Console": the Win32 text console used by the dedicated
// server and for debug output (original file 0x00744770-0x007452ef).
#pragma once

#include <list>
#include <vector>
#include <windows.h>

// Something drawn into the console window after the input line.
class C_ConsoleWidget {
public:
    virtual ~C_ConsoleWidget() = default;
    virtual void Draw(HDC dc) = 0;                       // 0x04
};

class C_ConsoleBase {                                    // vtable 0x008165e8
public:
    virtual ~C_ConsoleBase() = default;
    virtual void Create() {}
    virtual void Unknown08() {}                          // 0x007451e0
    virtual void Unknown0C() {}                          // 0x007451f0
};

class C_Console : public C_ConsoleBase {                // vtable 0x008165dc
public:
    static constexpr int MAX_HISTORY_LINES = 100;
    static constexpr int MAX_INPUT_LENGTH = 50;
    static constexpr int INPUT_BUFFER_SIZE = 0x200;

    C_Console();                                         // 0x007447a0
    ~C_Console() override;                               // 0x007448f0

    void Create() override;                              // 0x007450c0 registers the class, opens the window
    void Unknown08() override {}                         // 0x007451d0

    // Appends a line to the output history (thread safe).
    void AddLine(const char *text);                      // 0x00744cd0
    // Next command line typed by the user (FIFO); null if none. Caller frees it.
    char *PopCommand();                                  // 0x00744c10
    void OnChar(char ch);                                // 0x00744c60
    void Paint(HDC dc, const RECT &client);              // 0x00744fe0
    void AddWidget(C_ConsoleWidget *widget) { m_widgets.push_back(widget); }

    HWND GetWindow() const { return m_window; }
    void SetScrollPosition(int pos) { m_scrollPos = pos; }

private:
    void SubmitInputLine();                              // 0x00744e60
    void MoveInputLineToHistory();                       // 0x00744da0
    void RedrawInputLine();                              // 0x00744ed0
    void ResetInputLine();
    RECT InputLineRect(const RECT &client) const;

    HWND m_window = nullptr;                             // +0x04
    HFONT m_font = nullptr;                              // +0x08
    RECT m_client = {};                                  // +0x0C
    int m_scrollPos = 0;                                 // +0x28
    std::list<char *> m_history;                         // +0x2C newest first
    std::list<char *> m_commands;                        // +0x38 newest first
    char *m_input = nullptr;                             // +0x44
    CRITICAL_SECTION m_lock;                             // +0x48
    std::vector<C_ConsoleWidget *> m_widgets;            // +0x60
};

C_Console *GetConsole();                                 // 0x0069f4b0
LRESULT CALLBACK ConsoleWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);   // 0x00744aa0
