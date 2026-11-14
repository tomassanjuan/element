# Local: gracefully quit running Element, install the Release build, start it.
# WM_QUERYENDSESSION makes JUCE call systemRequestedQuit() (save prompt included),
# and works while the window is hidden in the tray (WM_CLOSE would only tray it).
Add-Type @"
using System;using System.Runtime.InteropServices;
public class W{public delegate bool P(IntPtr h,IntPtr l);
[DllImport("user32")]public static extern bool EnumWindows(P p,IntPtr l);
[DllImport("user32")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
[DllImport("user32")]public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out IntPtr r);}
"@
$root = Split-Path $PSScriptRoot
$dest = 'C:\Program Files\Kushview\Element\element.exe'
$ids = @(Get-Process element -ErrorAction SilentlyContinue | ForEach-Object Id)
if ($ids) {
  [W]::EnumWindows({ param($h,$l); $p=0; [W]::GetWindowThreadProcessId($h,[ref]$p) | Out-Null
    if ($ids -contains $p) { $r=[IntPtr]::Zero; [W]::SendMessageTimeout($h,0x11,[IntPtr]::Zero,[IntPtr]::Zero,2,1000,[ref]$r) | Out-Null }
    $true }, [IntPtr]::Zero) | Out-Null
  for ($i=0; $i -lt 10 -and (Get-Process element -ErrorAction SilentlyContinue); $i++) { Start-Sleep 1 }
}
if (Get-Process element -ErrorAction SilentlyContinue) { 'Element is still running (save prompt?). Not replaced.'; exit 1 }
Copy-Item "$root\build\element_app_artefacts\Release\element.exe" $dest -Force
Start-Process $dest
'Installed and started.'
