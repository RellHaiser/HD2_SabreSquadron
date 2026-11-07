# Skips the game's opening videos: Esc until the main menu shows (the videos' black bar at the top of the window has gone). Usage: to_menu.ps1 <process name> [seconds]
param([string]$Name, [int]$Seconds = 150)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class M {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct P { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref P p);
}
"@
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
    $p = Get-Process -Name $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($p -and $p.MainWindowHandle -ne 0) {
        $h = $p.MainWindowHandle
        [M]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [M]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero); [void][M]::SetForegroundWindow($h)
        Start-Sleep -Milliseconds 200
        if ([M]::GetForegroundWindow() -ne $h) { "the game is not in front"; exit 3 }
        $c = New-Object M+P
        $c.X = 800; $c.Y = 70
        [void][M]::ClientToScreen($h, [ref]$c)
        $b = New-Object System.Drawing.Bitmap 1, 1
        $g = [System.Drawing.Graphics]::FromImage($b)
        $g.CopyFromScreen($c.X, $c.Y, 0, 0, $b.Size)
        $px = $b.GetPixel(0, 0)
        $g.Dispose(); $b.Dispose()
        if ($px.R + $px.G + $px.B -gt 200) { "menu"; exit 0 }
        [M]::keybd_event(0, 1, 8, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120; [M]::keybd_event(0, 1, 10, [UIntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 1500
}
"timed out"; exit 4
