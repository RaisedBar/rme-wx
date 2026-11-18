<#
.SYNOPSIS
  Accessibility audit for the running TotalMix Access application.

.DESCRIPTION
  Enumerates every native control in the running TotalMixAccess process and queries its
  accessible name through MSAA (AccessibleObjectFromWindow, used by JAWS / NVDA) and through
  the native UI Automation core (IUIAutomation, used by Narrator and modern screen readers).
  Reports any interactive control without a name.


.EXAMPLE
  powershell -File tools\a11y-audit.ps1
  powershell -File tools\a11y-audit.ps1 -ShowNames -Match "Frequency"
#>
param([string]$ProcName = "TotalMixAccess", [switch]$ShowNames, [string]$Match = "")

Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
[ComImport, Guid("618736E0-3C3D-11CF-810C-00AA00389B71"), InterfaceType(ComInterfaceType.InterfaceIsDual)]
public interface IAcc {
  [return: MarshalAs(UnmanagedType.IDispatch)] object get_accParent();
  int get_accChildCount();
  [return: MarshalAs(UnmanagedType.IDispatch)] object get_accChild(object c);
  [return: MarshalAs(UnmanagedType.BStr)] string get_accName(object c);
  [return: MarshalAs(UnmanagedType.BStr)] string get_accValue(object c);
  [return: MarshalAs(UnmanagedType.BStr)] string get_accDescription(object c);
  object get_accRole(object c);
}
[ComImport, Guid("d22108aa-8ac5-49a5-837b-37bbb3d7591e"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IUIAElement {
  void SetFocus(); void GetRuntimeId(); void FindFirst(); void FindAll(); void FindFirstBuildCache();
  void FindAllBuildCache(); void BuildUpdatedCache();
  [return: MarshalAs(UnmanagedType.Struct)] object GetCurrentPropertyValue(int propertyId);
}
[ComImport, Guid("30cbe57d-d9d0-452a-ab13-7ac5ac4825ee"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IUIA {
  void CompareElements(); void CompareRuntimeIds(); void GetRootElement();
  [return: MarshalAs(UnmanagedType.Interface)] IUIAElement ElementFromHandle(IntPtr hwnd);
}
public static class A11yAudit {
  public delegate bool P(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, P cb, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(P cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("oleacc.dll")] static extern int AccessibleObjectFromWindow(IntPtr h, uint id, ref Guid iid, [MarshalAs(UnmanagedType.Interface)] out object o);
  static IUIA uia = (IUIA)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("ff48dba4-60ef-4201-aa87-54103eef594e")));
  public static string Cls(IntPtr h) { var sb = new StringBuilder(256); GetClassName(h, sb, 256); return sb.ToString(); }
  public static List<IntPtr> All(uint pid) {
    var tops = new List<IntPtr>(); EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h, out p); if (p==pid) tops.Add(h); return true; }, IntPtr.Zero);
    var r = new List<IntPtr>(); foreach (var t in tops) EnumChildWindows(t, (h,l)=>{ r.Add(h); return true; }, IntPtr.Zero); return r;
  }
  // name, value, description, role
  public static string[] Msaa(IntPtr h) {
    Guid iid = new Guid("618736E0-3C3D-11CF-810C-00AA00389B71"); object o;
    if (AccessibleObjectFromWindow(h, 0xFFFFFFFC, ref iid, out o) != 0 || o == null) return null;
    var a = (IAcc)o; string n = null, v = null, d = null; int role = 0;
    try { n = a.get_accName(0); } catch {}
    try { v = a.get_accValue(0); } catch {}
    try { d = a.get_accDescription(0); } catch {}
    try { object r = a.get_accRole(0); if (r is int) role = (int)r; } catch {}
    return new string[] { n, v, d, role.ToString() };
  }
  public static string UiaName(IntPtr h) {
    try { var v = uia.ElementFromHandle(h).GetCurrentPropertyValue(30005); return v == null ? null : v.ToString(); }
    catch { return null; }
  }
}
"@

$p = Get-Process $ProcName -ErrorAction Stop | Select-Object -First 1
$interactive = '^(Button|ComboBox|Edit|msctls_trackbar32|SysListView32|msctls_progress32|RICHEDIT50W)$'
$total = 0; $byClass = @{}; $msaaMissing = @(); $uiaMissing = @()
foreach ($h in [A11yAudit]::All([uint32]$p.Id)) {
    $cls = [A11yAudit]::Cls($h)
    # Group boxes (BS_GROUPBOX) are labels, not controls.
    if ($cls -eq 'Button' -and (([A11yAudit]::GetWindowLong($h, -16) -band 0xF) -eq 7)) { continue }
    $m = [A11yAudit]::Msaa($h)
    if ($null -eq $m) { continue }
    $key = $cls
    if ($cls -notmatch $interactive) {
        if ($m[3] -ne '40') { continue }   # custom windows exposed as ROLE_SYSTEM_GRAPHIC
        $key = 'graphic'
    }
    $total++
    $byClass[$key] = 1 + [int]$byClass[$key]
    $uia = [A11yAudit]::UiaName($h)
    if ([string]::IsNullOrWhiteSpace($m[0])) { $msaaMissing += "$key hwnd=$h" }
    if ([string]::IsNullOrWhiteSpace($uia)) { $uiaMissing += "$key hwnd=$h" }
    if ($ShowNames -and ($Match -eq '' -or "$($m[0])" -match $Match)) {
        "{0,-18} MSAA='{1}' value='{2}' desc='{3}' | UIA='{4}'" -f $key, $m[0], $m[1], $m[2], $uia
    }
}
"Checked $total controls: " + (($byClass.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join ', ')
"Without MSAA name: $($msaaMissing.Count)"
$msaaMissing | Select-Object -First 30
"Without UI Automation name: $($uiaMissing.Count)"
$uiaMissing | Select-Object -First 30
if ($msaaMissing.Count -or $uiaMissing.Count) { exit 1 }
