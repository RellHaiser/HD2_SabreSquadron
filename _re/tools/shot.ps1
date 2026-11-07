# A picture of the running game's window. Usage: shot.ps1 <process name> <out.png> [-Front]   (-Front brings it to the front first)
param([string]$Name, [string]$Out, [switch]$Front)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
    [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct P { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref P p);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
}
"@
$p = Get-Process -Name $Name -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { "no process $Name"; exit 1 }
$h = $p.MainWindowHandle
"window $h title '$($p.MainWindowTitle)' responding $($p.Responding) visible $([W]::IsWindowVisible($h)) iconic $([W]::IsIconic($h)) working set $([math]::Round($p.WorkingSet64/1MB)) MB"
if ($h -eq 0) { exit 2 }
if ($Front) { [W]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [W]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero); [void][W]::SetForegroundWindow($h); Start-Sleep -Milliseconds 700 }
# Only the game: a window over it would be in the picture, so no picture unless it is in front.
if ([W]::GetForegroundWindow() -ne $h) { "the game is not in front: no picture taken"; exit 3 }
$c = New-Object W+P
[void][W]::ClientToScreen($h, [ref]$c)
$cr = New-Object W+R
[void][W]::GetClientRect($h, [ref]$cr)
$w = $cr.Rt - $cr.L; $e = $cr.B - $cr.T
"client $($c.X),$($c.Y) ${w}x$e"
$b = New-Object System.Drawing.Bitmap $w, $e
$g = [System.Drawing.Graphics]::FromImage($b)
$g.CopyFromScreen($c.X, $c.Y, 0, 0, $b.Size)
$b.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $b.Dispose()
