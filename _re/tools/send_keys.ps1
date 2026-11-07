# Presses keys in the running game (DirectInput: by scan code). Usage: send_keys.ps1 <process name> <scan code in hex>[,<scan code>...] [-Hold ms]
# Also mouse: "click:x:y" clicks at the game's cursor coordinates; "click:x:y:move" only moves there; "rclick:x:y", "rclick2:x:y": a right click, a double one.
param([string]$Name, [string]$Keys, [int]$Hold = 250)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class K {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [StructLayout(LayoutKind.Sequential)] public struct P { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref P p);
}
"@
$p = Get-Process -Name $Name -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "no process $Name"; exit 1 }
$h = $p.MainWindowHandle
[K]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [K]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero); [void][K]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 300
# Never into another program: nothing is sent unless the game has the focus.
if ([K]::GetForegroundWindow() -ne $h) { "the game is not in front: no keys sent"; exit 3 }
foreach ($key in $Keys.Split(',')) {
    if ($key.StartsWith('click:') -or $key.StartsWith('rclick')) {
        $parts = $key.Split(':')
        # The game's cursor adds up DirectInput's relative moves: to the corner first, then to the place.
        for ($i = 0; $i -lt 40; ++$i) { [K]::mouse_event(1, [uint32]4294967196, [uint32]4294967196, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 15 }
        $left = [int]$parts[1]; $down = [int]$parts[2]
        while ($left -gt 0 -or $down -gt 0) {
            $dx = [Math]::Min($left, 50); $dy = [Math]::Min($down, 50)
            [K]::mouse_event(1, [uint32]$dx, [uint32]$dy, 0, [UIntPtr]::Zero)
            $left -= $dx; $down -= $dy
            Start-Sleep -Milliseconds 15
        }
        # (The menu picks before it reads the mouse, so a hover needs a move after the arrival: a small one, there and back.)
        Start-Sleep -Milliseconds 100
        [K]::mouse_event(1, 1, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 100
        [K]::mouse_event(1, [uint32]4294967295, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 100
        [K]::mouse_event(1, 1, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200
        if ($parts.Count -gt 3) { continue }           # "click:x:y:move": only moves.
        if ($key.StartsWith('rclick')) {
            $times = if ($key.StartsWith('rclick2')) { 2 } else { 1 }
            for ($i = 0; $i -lt $times; ++$i) {
                [K]::mouse_event(8, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
                [K]::mouse_event(16, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
            }
            continue
        }
        [K]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds $Hold
        [K]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    } else {
        $scan = [Convert]::ToByte($key, 16)
        [K]::keybd_event(0, $scan, 8, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds $Hold
        [K]::keybd_event(0, $scan, 10, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 400
}
