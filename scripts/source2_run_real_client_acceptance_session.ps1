[CmdletBinding()]
param(
  [string]$BuildDir = "build\source2-vcpkg-user-verify",
  [string]$LoginConfigPath = "artifacts\source2-deploy-config\login_server.eq2emu-target.ini",
  [string]$WorldConfigPath = "artifacts\source2-deploy-config\world_server.eq2emu-target.ini",
  [string]$DbHost = "192.168.1.243",
  [int]$DbPort = 3306,
  [string]$LoginDbName = "eq2ls",
  [string]$WorldDbName = "eq2emu",
  [string]$DbUser = "eq2emu",
  [string]$DbPassword = "",
  [int]$ClientVersion = 546,
  [string]$LoginListenAddress = "0.0.0.0",
  [string]$WorldLoginAddress = "127.0.0.1",
  [int]$LoginPort = 9100,
  [string]$WorldListenAddress = "0.0.0.0",
  [int]$WorldPort = 9101,
  [string]$ClientTargetHost = "127.0.0.1",
  [string]$WorldAdvertisedAddress = "",
  [string]$WorldName = "Source2 World",
  [string]$WorldServerVersion = "source2",
  [string]$WorldAccount = "",
  [string]$WorldPassword = "",
  [string]$OutputDir = "artifacts\source2-real-client-acceptance",
  [string]$ClientDir = "E:\Games\Everquest II",
  [string]$ClientConfigPath = "",
  [string]$ClientExePath = "",
  [string[]]$ClientArguments = @(),
  [string[]]$ClientLogPaths = @(),
  [switch]$UpdateClientConfig,
  [switch]$RestoreClientConfigOnExit,
  [switch]$LaunchClient,
  [switch]$AutoLoginClient,
  [ValidateSet("PasswordOnly", "UsernamePassword")]
  [string]$AutoLoginMode = "PasswordOnly",
  [ValidateSet("SendKeys", "SendInput", "WindowMessage")]
  [string]$AutoLoginInputMethod = "SendKeys",
  [ValidateSet("Unicode", "VirtualKey", "ScanCode")]
  [string]$AutoLoginTextMode = "Unicode",
  [int]$AutoLoginDelayMs = 5000,
  [string]$AutoLoginUsername = "",
  [string]$AutoLoginPassword = "",
  [ValidateSet("Window", "Client", "Screen")]
  [string]$AutoLoginCoordinateMode = "Window",
  [int]$AutoLoginUsernameX = -1,
  [int]$AutoLoginUsernameY = -1,
  [int]$AutoLoginPasswordX = -1,
  [int]$AutoLoginPasswordY = -1,
  [int]$AutoLoginSubmitX = -1,
  [int]$AutoLoginSubmitY = -1,
  [switch]$SnapshotClientLogs,
  [switch]$NoDiagnosticEvents,
  [switch]$NoWait,
  [int]$RunForMs = 0,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
  param(
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }

  $repoRoot = Split-Path -Parent $PSScriptRoot
  return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Resolve-Source2Exe {
  param(
    [string]$Name
  )

  $path = Resolve-RepoPath (Join-Path $BuildDir "source2\apps\Debug\$Name")
  if (!(Test-Path -LiteralPath $path)) {
    throw "Missing $Name at '$path'. Build first with scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir $BuildDir"
  }
  return $path
}

function Resolve-ConfigPath {
  param(
    [string]$Path,
    [string]$FallbackPath
  )

  $resolved = Resolve-RepoPath $Path
  if (Test-Path -LiteralPath $resolved) {
    return $resolved
  }

  $fallback = Resolve-RepoPath $FallbackPath
  if (Test-Path -LiteralPath $fallback) {
    return $fallback
  }

  throw "Missing config '$resolved'. Copy the deploy configs with scripts\source2_prepare_deploy_config.ps1, or pass an existing config path."
}

function Resolve-ClientConfigPath {
  param(
    [string]$ClientDir,
    [string]$ClientConfigPath
  )

  if (![string]::IsNullOrWhiteSpace($ClientConfigPath)) {
    return [System.IO.Path]::GetFullPath($ClientConfigPath)
  }
  if ([string]::IsNullOrWhiteSpace($ClientDir)) {
    throw "ClientDir is required when ClientConfigPath is not provided."
  }
  return [System.IO.Path]::GetFullPath((Join-Path $ClientDir "eq2_default.ini"))
}

function Resolve-ClientExePath {
  param(
    [string]$ClientDir,
    [string]$ClientExePath
  )

  if (![string]::IsNullOrWhiteSpace($ClientExePath)) {
    return [System.IO.Path]::GetFullPath($ClientExePath)
  }
  if ([string]::IsNullOrWhiteSpace($ClientDir)) {
    throw "ClientDir is required when ClientExePath is not provided."
  }
  return [System.IO.Path]::GetFullPath((Join-Path $ClientDir "EverQuest2.exe"))
}

function Resolve-ClientLogPaths {
  param(
    [string]$ClientDir,
    [string[]]$ClientLogPaths
  )

  $paths = [System.Collections.Generic.List[string]]::new()
  if ($ClientLogPaths.Count -gt 0) {
    foreach ($path in $ClientLogPaths) {
      if (![string]::IsNullOrWhiteSpace($path)) {
        [void]$paths.Add([System.IO.Path]::GetFullPath($path))
      }
    }
    return $paths.ToArray()
  }

  if (![string]::IsNullOrWhiteSpace($ClientDir)) {
    foreach ($name in @("eq2_packet_log.jsonl", "alertlog.txt", "eq2_crash.log")) {
      [void]$paths.Add([System.IO.Path]::GetFullPath((Join-Path $ClientDir $name)))
    }
  }

  return $paths.ToArray()
}

function Invoke-Checked {
  param(
    [string]$Label,
    [scriptblock]$Command
  )

  Write-Host "== $Label =="
  & $Command
  if ($LASTEXITCODE -ne 0) {
    throw "$Label failed with exit code $LASTEXITCODE"
  }
}

function Get-IniValue {
  param(
    [string]$Path,
    [string]$Section,
    [string]$Key
  )

  $currentSection = ""
  foreach ($line in Get-Content -LiteralPath $Path) {
    $trimmed = $line.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith(";") -or $trimmed.StartsWith("#")) {
      continue
    }
    if ($trimmed -match '^\[(.+)\]$') {
      $currentSection = $Matches[1].Trim()
      continue
    }
    if ($currentSection -eq $Section -and $trimmed -match '^([^=]+)=(.*)$') {
      if ($Matches[1].Trim() -eq $Key) {
        return $Matches[2].Trim()
      }
    }
  }

  return $null
}

function Test-ConfiguredSecret {
  param(
    [AllowNull()][string]$Value
  )

  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $false
  }

  $normalized = $Value.Trim().ToLowerInvariant()
  return $normalized -notin @("change-me", "changeme", "placeholder", "<db-password>", "<world-account>", "<world-password>", "<password>")
}

function Resolve-SecretOrConfig {
  param(
    [string]$Value,
    [string]$EnvName,
    [string]$ParamName,
    [string]$ConfigPath,
    [string]$ConfigSection,
    [string]$ConfigKey,
    [string]$ConfigLabel
  )

  if (![string]::IsNullOrWhiteSpace($Value)) {
    return [pscustomobject]@{ Source = "argument"; Value = $Value }
  }
  $envValue = [System.Environment]::GetEnvironmentVariable($EnvName)
  if (![string]::IsNullOrWhiteSpace($envValue)) {
    return [pscustomobject]@{ Source = "environment"; Value = $envValue }
  }
  $configValue = Get-IniValue -Path $ConfigPath -Section $ConfigSection -Key $ConfigKey
  if (Test-ConfiguredSecret $configValue) {
    return [pscustomobject]@{ Source = "config"; Value = $null }
  }
  throw "$ParamName, $EnvName, or $ConfigLabel in '$ConfigPath' is required unless -DryRun is used."
}

function Resolve-AutoLoginValue {
  param(
    [string]$Value,
    [string]$EnvName,
    [string]$ParamName
  )

  if (![string]::IsNullOrWhiteSpace($Value)) {
    return $Value
  }
  $envValue = [System.Environment]::GetEnvironmentVariable($EnvName)
  if (![string]::IsNullOrWhiteSpace($envValue)) {
    return $envValue
  }
  throw "$ParamName or $EnvName is required when -AutoLoginClient is used."
}

function Format-ArgumentForDisplay {
  param(
    [AllowNull()][string]$Value
  )

  if ($null -eq $Value) {
    return "''"
  }
  if ($Value -match '^[A-Za-z0-9_./:\\-]+$') {
    return $Value
  }
  return "'" + ($Value -replace "'", "''") + "'"
}

function Format-CommandForDisplay {
  param(
    [string]$Executable,
    [string[]]$Arguments
  )

  $parts = @((Format-ArgumentForDisplay $Executable))
  foreach ($argument in $Arguments) {
    $parts += Format-ArgumentForDisplay $argument
  }
  return "& " + ($parts -join " ")
}

function Redact-ClientArgumentsForDisplay {
  param(
    [string[]]$Arguments
  )

  $redacted = [System.Collections.Generic.List[string]]::new()
  $redactNext = $false
  foreach ($argument in $Arguments) {
    if ($redactNext) {
      [void]$redacted.Add("<redacted>")
      $redactNext = $false
      continue
    }

    $value = [string]$argument
    if ($value -match '^(?i)[+-]?(cl_sessionid|cl_password|password|login_password|sessionid)$') {
      [void]$redacted.Add($value)
      $redactNext = $true
      continue
    }
    if ($value -match '^(?i)[+-]?(cl_sessionid|cl_password|password|login_password|sessionid)=') {
      $name = $value.Substring(0, $value.IndexOf("="))
      [void]$redacted.Add("$name=<redacted>")
      continue
    }

    [void]$redacted.Add($value)
  }

  if ($redactNext) {
    [void]$redacted.Add("<redacted>")
  }

  return $redacted.ToArray()
}

function ConvertTo-StartProcessArguments {
  param(
    [string[]]$Arguments
  )

  return @($Arguments | ForEach-Object {
    if ($null -eq $_) {
      return '""'
    }

    $value = [string]$_
    if ($value.Length -eq 0) {
      return '""'
    }

    if ($value -match '[\s"]') {
      return '"' + ($value -replace '"', '\"') + '"'
    }

    return $value
  })
}

function ConvertTo-SendKeysLiteral {
  param(
    [AllowNull()][string]$Value
  )

  if ($null -eq $Value) {
    return ""
  }

  $builder = [System.Text.StringBuilder]::new()
  foreach ($character in $Value.ToCharArray()) {
    $text = [string]$character
    switch ($text) {
      "+" { [void]$builder.Append("{+}") }
      "^" { [void]$builder.Append("{^}") }
      "%" { [void]$builder.Append("{%}") }
      "~" { [void]$builder.Append("{~}") }
      "(" { [void]$builder.Append("{(}") }
      ")" { [void]$builder.Append("{)}") }
      "[" { [void]$builder.Append("{[}") }
      "]" { [void]$builder.Append("{]}") }
      "{" { [void]$builder.Append("{{}") }
      "}" { [void]$builder.Append("{}}") }
      default { [void]$builder.Append($text) }
    }
  }

  return $builder.ToString()
}

function Add-NativeInputType {
  if ("Source2NativeInput" -as [type]) {
    return
  }

  Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Source2NativeInput {
  private const int INPUT_MOUSE = 0;
  private const int INPUT_KEYBOARD = 1;
  private const uint KEYEVENTF_KEYUP = 0x0002;
  private const uint KEYEVENTF_UNICODE = 0x0004;
  private const uint KEYEVENTF_SCANCODE = 0x0008;
  private const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
  private const uint MOUSEEVENTF_LEFTUP = 0x0004;
  private const uint MAPVK_VK_TO_VSC = 0;
  private const ushort VK_MENU = 0x12;
  private const uint WM_ACTIVATE = 0x0006;
  private const uint WM_SETFOCUS = 0x0007;
  private const uint WM_KEYDOWN = 0x0100;
  private const uint WM_KEYUP = 0x0101;
  private const uint WM_CHAR = 0x0102;
  private const uint WM_MOUSEMOVE = 0x0200;
  private const uint WM_LBUTTONDOWN = 0x0201;
  private const uint WM_LBUTTONUP = 0x0202;
  private const int WA_ACTIVE = 1;
  private const int MK_LBUTTON = 1;
  private const int SW_RESTORE = 9;

  [StructLayout(LayoutKind.Sequential)]
  private struct INPUT {
    public int type;
    public InputUnion U;
  }

  [StructLayout(LayoutKind.Explicit)]
  private struct InputUnion {
    [FieldOffset(0)] public MOUSEINPUT mi;
    [FieldOffset(0)] public KEYBDINPUT ki;
  }

  [StructLayout(LayoutKind.Sequential)]
  private struct MOUSEINPUT {
    public int dx;
    public int dy;
    public uint mouseData;
    public uint dwFlags;
    public uint time;
    public IntPtr dwExtraInfo;
  }

  [StructLayout(LayoutKind.Sequential)]
  private struct KEYBDINPUT {
    public ushort wVk;
    public ushort wScan;
    public uint dwFlags;
    public uint time;
    public IntPtr dwExtraInfo;
  }

  [StructLayout(LayoutKind.Sequential)]
  private struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
  }

  [StructLayout(LayoutKind.Sequential)]
  private struct POINT {
    public int X;
    public int Y;
  }

  [DllImport("user32.dll", SetLastError=true)]
  private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);

  [DllImport("user32.dll", SetLastError=true)]
  private static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

  [DllImport("user32.dll", CharSet=CharSet.Unicode)]
  private static extern short VkKeyScan(char ch);

  [DllImport("user32.dll")]
  private static extern uint MapVirtualKey(uint uCode, uint uMapType);

  [DllImport("user32.dll")]
  private static extern bool SetCursorPos(int x, int y);

  [DllImport("user32.dll")]
  private static extern bool SetForegroundWindow(IntPtr hWnd);

  [DllImport("user32.dll")]
  private static extern IntPtr GetForegroundWindow();

  [DllImport("user32.dll")]
  private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

  [DllImport("kernel32.dll")]
  private static extern uint GetCurrentThreadId();

  [DllImport("user32.dll")]
  private static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);

  [DllImport("user32.dll")]
  private static extern bool BringWindowToTop(IntPtr hWnd);

  [DllImport("user32.dll")]
  private static extern IntPtr SetActiveWindow(IntPtr hWnd);

  [DllImport("user32.dll")]
  private static extern IntPtr SetFocus(IntPtr hWnd);

  [DllImport("user32.dll")]
  private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

  [DllImport("user32.dll")]
  private static extern void SwitchToThisWindow(IntPtr hWnd, bool fAltTab);

  [DllImport("user32.dll")]
  private static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

  [DllImport("user32.dll")]
  private static extern bool GetClientRect(IntPtr hWnd, out RECT rect);

  [DllImport("user32.dll")]
  private static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);

  public static string FocusWindow(IntPtr hWnd) {
    if (hWnd == IntPtr.Zero) {
      return "focus_window=0x0";
    }
    IntPtr before = GetForegroundWindow();
    uint foregroundProcessId;
    uint targetProcessId;
    uint foregroundThread = GetWindowThreadProcessId(before, out foregroundProcessId);
    uint targetThread = GetWindowThreadProcessId(hWnd, out targetProcessId);
    uint currentThread = GetCurrentThreadId();

    ShowWindow(hWnd, SW_RESTORE);
    Key(VK_MENU);
    bool switched = false;
    try {
      SwitchToThisWindow(hWnd, true);
      switched = true;
    } catch {
      switched = false;
    }
    bool attachedToForeground = foregroundThread != 0 && AttachThreadInput(currentThread, foregroundThread, true);
    bool attachedToTarget = targetThread != 0 && targetThread != currentThread && AttachThreadInput(currentThread, targetThread, true);
    bool broughtToTop = BringWindowToTop(hWnd);
    IntPtr activeWindow = SetActiveWindow(hWnd);
    IntPtr focusWindow = SetFocus(hWnd);
    bool foregroundSet = SetForegroundWindow(hWnd);
    if (attachedToTarget) {
      AttachThreadInput(currentThread, targetThread, false);
    }
    if (attachedToForeground) {
      AttachThreadInput(currentThread, foregroundThread, false);
    }

    IntPtr after = GetForegroundWindow();
    return string.Format(
      "focus_before=0x{0:X}{1}focus_after=0x{2:X}{1}focus_matches_target={3}{1}focus_set_foreground={4}{1}focus_bring_to_top={5}{1}focus_switch_to_this_window={6}{1}focus_attached_foreground={7}{1}focus_attached_target={8}{1}focus_active_window=0x{9:X}{1}focus_set_focus=0x{10:X}",
      before.ToInt64(),
      Environment.NewLine,
      after.ToInt64(),
      after == hWnd ? "true" : "false",
      foregroundSet ? "true" : "false",
      broughtToTop ? "true" : "false",
      switched ? "true" : "false",
      attachedToForeground ? "true" : "false",
      attachedToTarget ? "true" : "false",
      activeWindow.ToInt64(),
      focusWindow.ToInt64());
  }

  public static void ClickScreen(int x, int y) {
    SetCursorPos(x, y);
    INPUT[] inputs = new INPUT[2];
    inputs[0].type = INPUT_MOUSE;
    inputs[0].U.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].U.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT)));
  }

  public static void ClickWindow(IntPtr hWnd, int x, int y) {
    RECT rect;
    if (hWnd != IntPtr.Zero && GetWindowRect(hWnd, out rect)) {
      ClickScreen(rect.Left + x, rect.Top + y);
      return;
    }
    ClickScreen(x, y);
  }

  public static void ClickClient(IntPtr hWnd, int x, int y) {
    POINT point = new POINT();
    point.X = x;
    point.Y = y;
    if (hWnd != IntPtr.Zero && ClientToScreen(hWnd, ref point)) {
      ClickScreen(point.X, point.Y);
      return;
    }
    ClickScreen(x, y);
  }

  private static IntPtr MakeLParam(int low, int high) {
    return (IntPtr)(((high & 0xffff) << 16) | (low & 0xffff));
  }

  private static IntPtr MakeKeyLParam(ushort vk, bool keyUp) {
    uint scan = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    uint value = 1u | (scan << 16);
    if (keyUp) {
      value |= 0xC0000000u;
    }
    return unchecked((IntPtr)(int)value);
  }

  public static string PostActivate(IntPtr hWnd) {
    if (hWnd == IntPtr.Zero) {
      return "post_activate=false";
    }

    bool activated = PostMessage(hWnd, WM_ACTIVATE, (IntPtr)WA_ACTIVE, IntPtr.Zero);
    bool focused = PostMessage(hWnd, WM_SETFOCUS, IntPtr.Zero, IntPtr.Zero);
    return string.Format("post_activate={0}{1}post_set_focus={2}",
      activated ? "true" : "false",
      Environment.NewLine,
      focused ? "true" : "false");
  }

  public static void PostClickClient(IntPtr hWnd, int x, int y) {
    if (hWnd == IntPtr.Zero || x < 0 || y < 0) {
      return;
    }

    IntPtr point = MakeLParam(x, y);
    PostMessage(hWnd, WM_MOUSEMOVE, IntPtr.Zero, point);
    PostMessage(hWnd, WM_LBUTTONDOWN, (IntPtr)MK_LBUTTON, point);
    PostMessage(hWnd, WM_LBUTTONUP, IntPtr.Zero, point);
  }

  public static void PostKey(IntPtr hWnd, ushort vk) {
    if (hWnd == IntPtr.Zero) {
      return;
    }

    PostMessage(hWnd, WM_KEYDOWN, (IntPtr)vk, MakeKeyLParam(vk, false));
    PostMessage(hWnd, WM_KEYUP, (IntPtr)vk, MakeKeyLParam(vk, true));
  }

  public static void PostText(IntPtr hWnd, string text) {
    if (hWnd == IntPtr.Zero || String.IsNullOrEmpty(text)) {
      return;
    }

    foreach (char ch in text) {
      PostMessage(hWnd, WM_CHAR, (IntPtr)ch, IntPtr.Zero);
    }
  }

  public static string DescribeWindow(IntPtr hWnd) {
    if (hWnd == IntPtr.Zero) {
      return "window_handle=0x0";
    }

    RECT windowRect;
    RECT clientRect;
    POINT clientOrigin = new POINT();
    clientOrigin.X = 0;
    clientOrigin.Y = 0;

    bool hasWindowRect = GetWindowRect(hWnd, out windowRect);
    bool hasClientRect = GetClientRect(hWnd, out clientRect);
    bool hasClientOrigin = ClientToScreen(hWnd, ref clientOrigin);

    string windowLine = hasWindowRect
      ? string.Format("window_rect={0},{1},{2},{3} size={4}x{5}",
          windowRect.Left,
          windowRect.Top,
          windowRect.Right,
          windowRect.Bottom,
          windowRect.Right - windowRect.Left,
          windowRect.Bottom - windowRect.Top)
      : "window_rect=unavailable";
    string clientLine = hasClientRect
      ? string.Format("client_rect={0},{1},{2},{3} size={4}x{5}",
          clientRect.Left,
          clientRect.Top,
          clientRect.Right,
          clientRect.Bottom,
          clientRect.Right - clientRect.Left,
          clientRect.Bottom - clientRect.Top)
      : "client_rect=unavailable";
    string originLine = hasClientOrigin
      ? string.Format("client_origin_screen={0},{1}", clientOrigin.X, clientOrigin.Y)
      : "client_origin_screen=unavailable";

    return string.Format(
      "window_handle=0x{0:X}{1}{2}{1}{3}{1}{4}",
      hWnd.ToInt64(),
      Environment.NewLine,
      windowLine,
      clientLine,
      originLine);
  }

  public static void Key(ushort vk) {
    INPUT[] inputs = new INPUT[2];
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].U.ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].U.ki.wVk = vk;
    inputs[1].U.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT)));
  }

  public static void KeyScan(ushort vk) {
    ushort scan = (ushort)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    INPUT[] inputs = new INPUT[2];
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].U.ki.wScan = scan;
    inputs[0].U.ki.dwFlags = KEYEVENTF_SCANCODE;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].U.ki.wScan = scan;
    inputs[1].U.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT)));
  }

  public static void VirtualKeyText(string text) {
    if (String.IsNullOrEmpty(text)) {
      return;
    }
    foreach (char ch in text) {
      short mapped = VkKeyScan(ch);
      if (mapped == -1) {
        continue;
      }

      ushort vk = (ushort)(mapped & 0xff);
      byte shiftState = (byte)((mapped >> 8) & 0xff);
      bool shift = (shiftState & 0x01) != 0;
      bool ctrl = (shiftState & 0x02) != 0;
      bool alt = (shiftState & 0x04) != 0;
      INPUT[] inputs = new INPUT[(shift ? 2 : 0) + (ctrl ? 2 : 0) + (alt ? 2 : 0) + 2];
      int index = 0;

      if (shift) {
        inputs[index].type = INPUT_KEYBOARD;
        inputs[index].U.ki.wVk = 0x10;
        index++;
      }
      if (ctrl) {
        inputs[index].type = INPUT_KEYBOARD;
        inputs[index].U.ki.wVk = 0x11;
        index++;
      }
      if (alt) {
        inputs[index].type = INPUT_KEYBOARD;
        inputs[index].U.ki.wVk = 0x12;
        index++;
      }

      inputs[index].type = INPUT_KEYBOARD;
      inputs[index].U.ki.wVk = vk;
      index++;
      inputs[index].type = INPUT_KEYBOARD;
      inputs[index].U.ki.wVk = vk;
      inputs[index].U.ki.dwFlags = KEYEVENTF_KEYUP;
      index++;

      if (alt) {
        inputs[index].type = INPUT_KEYBOARD;
        inputs[index].U.ki.wVk = 0x12;
        inputs[index].U.ki.dwFlags = KEYEVENTF_KEYUP;
        index++;
      }
      if (ctrl) {
        inputs[index].type = INPUT_KEYBOARD;
        inputs[index].U.ki.wVk = 0x11;
        inputs[index].U.ki.dwFlags = KEYEVENTF_KEYUP;
        index++;
      }
      if (shift) {
        inputs[index].type = INPUT_KEYBOARD;
        inputs[index].U.ki.wVk = 0x10;
        inputs[index].U.ki.dwFlags = KEYEVENTF_KEYUP;
        index++;
      }

      SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT)));
    }
  }

  public static void ScanCodeText(string text) {
    if (String.IsNullOrEmpty(text)) {
      return;
    }
    foreach (char ch in text) {
      short mapped = VkKeyScan(ch);
      if (mapped == -1) {
        continue;
      }

      ushort vk = (ushort)(mapped & 0xff);
      byte shiftState = (byte)((mapped >> 8) & 0xff);
      bool shift = (shiftState & 0x01) != 0;
      bool ctrl = (shiftState & 0x02) != 0;
      bool alt = (shiftState & 0x04) != 0;
      INPUT[] inputs = new INPUT[(shift ? 2 : 0) + (ctrl ? 2 : 0) + (alt ? 2 : 0) + 2];
      int index = 0;

      Action<ushort, bool> addScanKey = (key, keyUp) => {
        inputs[index].type = INPUT_KEYBOARD;
        inputs[index].U.ki.wScan = (ushort)MapVirtualKey(key, MAPVK_VK_TO_VSC);
        inputs[index].U.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0u);
        index++;
      };

      if (shift) {
        addScanKey(0x10, false);
      }
      if (ctrl) {
        addScanKey(0x11, false);
      }
      if (alt) {
        addScanKey(0x12, false);
      }

      addScanKey(vk, false);
      addScanKey(vk, true);

      if (alt) {
        addScanKey(0x12, true);
      }
      if (ctrl) {
        addScanKey(0x11, true);
      }
      if (shift) {
        addScanKey(0x10, true);
      }

      SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT)));
    }
  }

  public static void CtrlA() {
    INPUT[] inputs = new INPUT[4];
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].U.ki.wVk = 0x11;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].U.ki.wVk = 0x41;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].U.ki.wVk = 0x41;
    inputs[2].U.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].U.ki.wVk = 0x11;
    inputs[3].U.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT)));
  }

  public static void UnicodeText(string text) {
    if (String.IsNullOrEmpty(text)) {
      return;
    }
    foreach (char ch in text) {
      INPUT[] inputs = new INPUT[2];
      inputs[0].type = INPUT_KEYBOARD;
      inputs[0].U.ki.wScan = ch;
      inputs[0].U.ki.dwFlags = KEYEVENTF_UNICODE;
      inputs[1].type = INPUT_KEYBOARD;
      inputs[1].U.ki.wScan = ch;
      inputs[1].U.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
      SendInput((uint)inputs.Length, inputs, Marshal.SizeOf(typeof(INPUT)));
    }
  }
}
"@
}

function Invoke-ClientAutoLogin {
  param(
    [System.Diagnostics.Process]$ClientProcess,
    [string]$Mode,
    [string]$InputMethod,
    [string]$TextMode,
    [int]$DelayMs,
    [string]$Username,
    [string]$Password,
    [string]$CoordinateMode,
    [int]$UsernameX,
    [int]$UsernameY,
    [int]$PasswordX,
    [int]$PasswordY,
    [int]$SubmitX,
    [int]$SubmitY,
    [string]$DiagnosticsPath = ""
  )

  if ($null -eq $ClientProcess) {
    throw "-AutoLoginClient requires -LaunchClient."
  }
  if ($DelayMs -gt 0) {
    Start-Sleep -Milliseconds $DelayMs
  }
  if ($ClientProcess.HasExited) {
    throw "EQ2 client exited before auto-login keys could be sent."
  }

  $shell = New-Object -ComObject WScript.Shell
  $activated = $shell.AppActivate($ClientProcess.Id)
  if (!$activated) {
    $activated = $shell.AppActivate("EverQuest")
  }
  if (!$activated) {
    throw "Could not activate the EQ2 client window for auto-login."
  }

  Start-Sleep -Milliseconds 500

  if ($InputMethod -eq "SendInput" -or $InputMethod -eq "WindowMessage") {
    Add-NativeInputType
    $ClientProcess.Refresh()
    $windowHandle = $ClientProcess.MainWindowHandle
    $focusDiagnostics = [Source2NativeInput]::FocusWindow($windowHandle)
    $messageDiagnostics = ""
    if ($InputMethod -eq "WindowMessage") {
      $messageDiagnostics = [Source2NativeInput]::PostActivate($windowHandle)
    }
    Start-Sleep -Milliseconds 300
    $windowDiagnostics = [Source2NativeInput]::DescribeWindow($windowHandle)
    $diagnosticLines = @(
      "input_method=$InputMethod",
      "text_mode=$TextMode",
      "coordinate_mode=$CoordinateMode",
      "username_xy=$UsernameX,$UsernameY",
      "password_xy=$PasswordX,$PasswordY",
      "submit_xy=$SubmitX,$SubmitY"
    ) + ($focusDiagnostics -split [Environment]::NewLine) + ($messageDiagnostics -split [Environment]::NewLine) + ($windowDiagnostics -split [Environment]::NewLine)
    Write-Host "Auto-login window diagnostics:"
    foreach ($line in $diagnosticLines) {
      if (![string]::IsNullOrWhiteSpace($line)) {
        Write-Host "  $line"
      }
    }
    if (![string]::IsNullOrWhiteSpace($DiagnosticsPath)) {
      Set-Content -LiteralPath $DiagnosticsPath -Value $diagnosticLines
    }

    $click = {
      param(
        [IntPtr]$Handle,
        [string]$Mode,
        [int]$X,
        [int]$Y
      )

      if ($X -lt 0 -or $Y -lt 0) {
        return $false
      }
      if ($InputMethod -eq "WindowMessage") {
        if ($Mode -ne "Client") {
          throw "WindowMessage auto-login requires -AutoLoginCoordinateMode Client."
        }
        [Source2NativeInput]::PostClickClient($Handle, $X, $Y)
        Start-Sleep -Milliseconds 150
        return $true
      }
      if ($Mode -eq "Screen") {
        [Source2NativeInput]::ClickScreen($X, $Y)
      } elseif ($Mode -eq "Client") {
        [Source2NativeInput]::ClickClient($Handle, $X, $Y)
      } else {
        [Source2NativeInput]::ClickWindow($Handle, $X, $Y)
      }
      Start-Sleep -Milliseconds 150
      return $true
    }

    $sendInputText = {
      param([string]$Value)

      if ($InputMethod -eq "WindowMessage") {
        [Source2NativeInput]::PostText($windowHandle, $Value)
        return
      }
      if ($TextMode -eq "ScanCode") {
        [Source2NativeInput]::ScanCodeText($Value)
      } elseif ($TextMode -eq "VirtualKey") {
        [Source2NativeInput]::VirtualKeyText($Value)
      } else {
        [Source2NativeInput]::UnicodeText($Value)
      }
    }

    if ($Mode -eq "UsernamePassword") {
      $clickedUsername = & $click $windowHandle $CoordinateMode $UsernameX $UsernameY
      if (!$clickedUsername) {
        [Source2NativeInput]::CtrlA()
      }
      & $sendInputText $Username
      $clickedPassword = & $click $windowHandle $CoordinateMode $PasswordX $PasswordY
      if (!$clickedPassword) {
        if ($InputMethod -eq "WindowMessage") {
          [Source2NativeInput]::PostKey($windowHandle, 0x09)
        } elseif ($TextMode -eq "ScanCode") {
          [Source2NativeInput]::KeyScan(0x09)
        } else {
          [Source2NativeInput]::Key(0x09)
        }
      }
    } elseif ($PasswordX -ge 0 -and $PasswordY -ge 0) {
      [void](& $click $windowHandle $CoordinateMode $PasswordX $PasswordY)
    }

    & $sendInputText $Password
    if ($SubmitX -ge 0 -and $SubmitY -ge 0) {
      [void](& $click $windowHandle $CoordinateMode $SubmitX $SubmitY)
    } else {
      if ($InputMethod -eq "WindowMessage") {
        [Source2NativeInput]::PostKey($windowHandle, 0x0d)
      } elseif ($TextMode -eq "ScanCode") {
        [Source2NativeInput]::KeyScan(0x0d)
      } else {
        [Source2NativeInput]::Key(0x0d)
      }
    }
    return
  }

  $sendKeys = {
    param(
      [object]$Shell,
      [string]$Keys
    )

    try {
      Add-Type -AssemblyName System.Windows.Forms
      [System.Windows.Forms.SendKeys]::SendWait($Keys)
    } catch {
      $Shell.SendKeys($Keys)
    }
  }

  if ($Mode -eq "UsernamePassword") {
    & $sendKeys $shell "^a"
    & $sendKeys $shell (ConvertTo-SendKeysLiteral $Username)
    & $sendKeys $shell "{TAB}"
  }
  & $sendKeys $shell (ConvertTo-SendKeysLiteral $Password)
  & $sendKeys $shell "{ENTER}"
}

function Add-RequiredSecretName {
  param(
    [System.Collections.Generic.List[string]]$Names,
    [string]$Name
  )

  if (!$Names.Contains($Name)) {
    [void]$Names.Add($Name)
  }
}

function Wait-TcpEndpoint {
  param(
    [string]$TargetHost,
    [int]$TargetPort,
    [int]$TimeoutMs = 10000
  )

  $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  while ([DateTime]::UtcNow -lt $deadline) {
    $client = $null
    try {
      $client = [System.Net.Sockets.TcpClient]::new()
      $async = $client.BeginConnect($TargetHost, $TargetPort, $null, $null)
      if ($async.AsyncWaitHandle.WaitOne(250, $false)) {
        $client.EndConnect($async)
        return $true
      }
    } catch {
      Start-Sleep -Milliseconds 100
    } finally {
      if ($client) {
        $client.Close()
      }
    }
  }

  return $false
}

function Start-LoggedProcess {
  param(
    [string]$Label,
    [string]$Executable,
    [string[]]$Arguments,
    [string]$StdOut,
    [string]$StdErr
  )

  Write-Host "== Starting $Label =="
  Write-Host (Format-CommandForDisplay -Executable $Executable -Arguments $Arguments)
  return Start-Process -FilePath $Executable `
    -ArgumentList (ConvertTo-StartProcessArguments $Arguments) `
    -RedirectStandardOutput $StdOut `
    -RedirectStandardError $StdErr `
    -WindowStyle Hidden `
    -PassThru
}

function Stop-Source2Process {
  param(
    [AllowNull()]$Process,
    [string]$Label
  )

  if ($null -eq $Process) {
    return
  }
  try {
    if (!$Process.HasExited) {
      Write-Host "Stopping $Label pid=$($Process.Id)"
      Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
      [void]$Process.WaitForExit(5000)
    }
  } catch {
    Write-Host "Warning: failed to stop $Label pid=$($Process.Id): $($_.Exception.Message)"
  }
}

function Invoke-ClientConfigHelper {
  param(
    [string]$Label,
    [string[]]$Arguments
  )

  $helper = Resolve-RepoPath "scripts\source2_set_eq2_client_login.ps1"
  if (!(Test-Path -LiteralPath $helper)) {
    throw "Missing client config helper '$helper'."
  }

  Write-Host "== $Label =="
  & powershell -NoProfile -ExecutionPolicy Bypass -File $helper @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Label failed with exit code $LASTEXITCODE"
  }
}

function Copy-ClientLogSnapshots {
  param(
    [string[]]$Paths,
    [string]$OutputDir
  )

  New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
  $manifest = [System.Collections.Generic.List[string]]::new()
  [void]$manifest.Add("Source`tExists`tLength`tLastWriteTime`tSnapshot")

  foreach ($path in $Paths) {
    $snapshot = ""
    $exists = Test-Path -LiteralPath $path
    $length = ""
    $lastWrite = ""

    if ($exists) {
      $item = Get-Item -LiteralPath $path
      $length = "$($item.Length)"
      $lastWrite = $item.LastWriteTime.ToString("o")
      $snapshot = Join-Path $OutputDir (Split-Path -Leaf $path)
      Copy-Item -LiteralPath $path -Destination $snapshot -Force
    }

    [void]$manifest.Add(("{0}`t{1}`t{2}`t{3}`t{4}" -f $path,$exists,$length,$lastWrite,$snapshot))
  }

  Set-Content -LiteralPath (Join-Path $OutputDir "manifest.tsv") -Value $manifest
}

function Write-SessionReport {
  param(
    [string]$Path,
    [string]$StartedAt,
    [string]$LoginStdOut,
    [string]$LoginStdErr,
    [string]$WorldStdOut,
    [string]$WorldStdErr,
    [int]$LoginPid,
    [int]$WorldPid,
    [string]$ClientConfig,
    [string]$ClientExe,
    [string]$ClientPid,
    [string]$ClientArgumentsDisplay,
    [string]$ClientLogSnapshotDir,
    [string]$AutoLoginDiagnosticsPath = "",
    [string]$SessionStatus = "ready",
    [string]$FailureReason = ""
  )

  $content = @(
    "# Source2 Real-Client Acceptance Session",
    "",
    "Started: $StartedAt",
    "Status: $SessionStatus",
    "",
    "## Runtime",
    "",
    "- Login PID: ``$LoginPid``",
    "- World PID: ``$WorldPid``",
    "- Login listen: ``${LoginListenAddress}:$LoginPort``",
    "- Client login setting: ``cl_ls_address $ClientTargetHost``",
    "- Client config: ``$ClientConfig``",
    "- Client executable: ``$ClientExe``",
    "- Client PID: ``$ClientPid``",
    "- Client arguments: ``$ClientArgumentsDisplay``",
    "- World listen: ``${WorldListenAddress}:$WorldPort``",
    "- World advertised address: ``$WorldAdvertisedAddress``",
    "- Login config: ``$LoginConfigPath``",
    "- World config: ``$WorldConfigPath``",
    "",
    "## Logs",
    "",
    "- Login stdout: ``$LoginStdOut``",
    "- Login stderr: ``$LoginStdErr``",
    "- World stdout: ``$WorldStdOut``",
    "- World stderr: ``$WorldStdErr``",
    "- Client log snapshots: ``$ClientLogSnapshotDir``",
    ""
  )

  if (![string]::IsNullOrWhiteSpace($AutoLoginDiagnosticsPath)) {
    $content = $content[0..($content.Count - 2)] + @(
      "- Auto-login diagnostics: ``$AutoLoginDiagnosticsPath``",
      ""
    )
  }

  if (![string]::IsNullOrWhiteSpace($FailureReason)) {
    $content += @(
      "## Failure",
      "",
      "``$FailureReason``",
      ""
    )
  }

  $content += @(
    "## Manual Results",
    "",
    "| Gate | Result | Notes |",
    "| --- | --- | --- |",
    "| Login progresses past ``Trying login server #1`` | Pending | |",
    "| World list shows ``$WorldName`` | Pending | |",
    "| Character list appears for the test account | Pending | |",
    "| Play/select reaches world handoff | Pending | |",
    "| Create/delete test character works, if exercised | Pending | |",
    "| Wrong-password attempt logs ``login_rejected`` without a password value | Pending | |",
    "",
    "Update ``migrationv3/real_client_acceptance_report.md`` with the final pass/fail results and artifact paths."
  )

  Set-Content -LiteralPath $Path -Value $content
}

if ([string]::IsNullOrWhiteSpace($WorldAdvertisedAddress)) {
  $WorldAdvertisedAddress = $ClientTargetHost
}

if ($RunForMs -lt 0) {
  throw "RunForMs must be zero or greater."
}
if ($RunForMs -gt 0 -and $RunForMs -lt 3000) {
  throw "RunForMs must be zero for an interactive run or at least 3000 milliseconds for a bounded preflight."
}

if (!$DryRun -and !$NoWait -and $RunForMs -eq 0 -and [Console]::IsInputRedirected) {
  throw "Interactive input is redirected; use -RunForMs or -NoWait."
}

$loginServer = Resolve-Source2Exe "eq2_login_server.exe"
$worldServer = Resolve-Source2Exe "eq2_world_server.exe"
$LoginConfigPath = Resolve-ConfigPath -Path $LoginConfigPath -FallbackPath "source2\config\login_server.eq2emu-target.ini.example"
$WorldConfigPath = Resolve-ConfigPath -Path $WorldConfigPath -FallbackPath "source2\config\world_server.eq2emu-target.ini.example"
$resolvedClientConfigPath = Resolve-ClientConfigPath -ClientDir $ClientDir -ClientConfigPath $ClientConfigPath
$resolvedClientExePath = Resolve-ClientExePath -ClientDir $ClientDir -ClientExePath $ClientExePath
$resolvedClientLogPaths = Resolve-ClientLogPaths -ClientDir $ClientDir -ClientLogPaths $ClientLogPaths
$OutputDir = Resolve-RepoPath $OutputDir

$loginArgs = @(
  "--serve",
  "--config", $LoginConfigPath,
  "--login-address", $LoginListenAddress,
  "--login-port", "$LoginPort",
  "--db-host", $DbHost,
  "--db-port", "$DbPort",
  "--db-name", $LoginDbName,
  "--db-user", $DbUser,
  "--client-version", "$ClientVersion"
)
if (!$NoDiagnosticEvents) {
  $loginArgs += "--diagnostic-events"
}

$worldArgs = @(
  "--serve",
  "--config", $WorldConfigPath,
  "--login-address", $WorldLoginAddress,
  "--login-port", "$LoginPort",
  "--world-address", $WorldListenAddress,
  "--world-port", "$WorldPort",
  "--world-advertised-address", $WorldAdvertisedAddress,
  "--world-name", $WorldName,
  "--world-server-version", $WorldServerVersion,
  "--db-host", $DbHost,
  "--db-port", "$DbPort",
  "--db-name", $WorldDbName,
  "--db-user", $DbUser
)

if ($RunForMs -gt 0) {
  $loginArgs += @("--run-for-ms", "$RunForMs")
  $worldArgs += @("--run-for-ms", "$RunForMs")
}

$startedAt = Get-Date -Format "yyyyMMdd-HHmmss"
$sessionDir = Join-Path $OutputDir $startedAt
$loginStdOut = Join-Path $sessionDir "login.stdout.log"
$loginStdErr = Join-Path $sessionDir "login.stderr.log"
$worldStdOut = Join-Path $sessionDir "world.stdout.log"
$worldStdErr = Join-Path $sessionDir "world.stderr.log"
$sessionReport = Join-Path $sessionDir "session.md"
$clientLogSnapshotDir = Join-Path $sessionDir "client-logs"
$autoLoginDiagnosticsPath = Join-Path $sessionDir "auto-login-input.txt"

if ($DryRun) {
  Write-Host "== Source2 real-client acceptance session dry run =="
  Write-Host "Client login setting: cl_ls_address $ClientTargetHost"
  Write-Host "World advertised address: $WorldAdvertisedAddress"
  Write-Host "Client config: $resolvedClientConfigPath"
  Write-Host "Client executable: $resolvedClientExePath"
  Write-Host "Planned artifact directory: $sessionDir"
  Write-Host "Required environment secrets unless provided by config:"
  $requiredSecrets = [System.Collections.Generic.List[string]]::new()
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $LoginConfigPath -Section "db" -Key "password"))) {
    Add-RequiredSecretName -Names $requiredSecrets -Name "EQ2_DB_PASSWORD"
  }
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $WorldConfigPath -Section "world_db" -Key "password"))) {
    Add-RequiredSecretName -Names $requiredSecrets -Name "EQ2_DB_PASSWORD"
  }
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $WorldConfigPath -Section "world" -Key "account"))) {
    Add-RequiredSecretName -Names $requiredSecrets -Name "EQ2_WORLD_ACCOUNT"
  }
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $WorldConfigPath -Section "world" -Key "password"))) {
    Add-RequiredSecretName -Names $requiredSecrets -Name "EQ2_WORLD_PASSWORD"
  }
  if ($requiredSecrets.Count -eq 0) {
    Write-Host "  (none)"
  } else {
    foreach ($name in $requiredSecrets) {
      Write-Host "  $name"
    }
  }
  Write-Host "Planned login command:"
  Write-Host (Format-CommandForDisplay -Executable $loginServer -Arguments $loginArgs)
  Write-Host "Planned world command:"
  Write-Host (Format-CommandForDisplay -Executable $worldServer -Arguments $worldArgs)
  if ($UpdateClientConfig) {
    Write-Host "Planned client config update:"
    Write-Host (Format-CommandForDisplay -Executable "scripts\source2_set_eq2_client_login.ps1" -Arguments @("-ConfigPath", $resolvedClientConfigPath, "-LoginHost", $ClientTargetHost))
    if ($RestoreClientConfigOnExit) {
      Write-Host "Planned client config restore on exit:"
      Write-Host (Format-CommandForDisplay -Executable "scripts\source2_set_eq2_client_login.ps1" -Arguments @("-ConfigPath", $resolvedClientConfigPath, "-RestoreOriginal"))
    }
  }
  if ($LaunchClient) {
    Write-Host "Planned client launch:"
    Write-Host (Format-CommandForDisplay -Executable $resolvedClientExePath -Arguments (Redact-ClientArgumentsForDisplay $ClientArguments))
  }
  if ($AutoLoginClient) {
    Write-Host "Planned client auto-login: enabled after $AutoLoginDelayMs ms via $AutoLoginInputMethod; password redacted."
    if ($AutoLoginInputMethod -ne "SendKeys") {
      Write-Host "Planned client auto-login coordinate mode: $AutoLoginCoordinateMode"
    }
    if ($AutoLoginInputMethod -eq "SendInput") {
      Write-Host "Planned client auto-login text mode: $AutoLoginTextMode"
    }
  }
  if ($SnapshotClientLogs) {
    Write-Host "Planned client log snapshots:"
    foreach ($path in $resolvedClientLogPaths) {
      Write-Host "  $path"
    }
    Write-Host "  Output: $clientLogSnapshotDir"
  }
  return
}

New-Item -ItemType Directory -Force -Path $sessionDir | Out-Null

Invoke-Checked "Validate login config" {
  & $loginServer --validate-config --config $LoginConfigPath
}
Invoke-Checked "Validate world config" {
  & $worldServer --validate-config --config $WorldConfigPath
}

$previousDbPassword = $env:EQ2_DB_PASSWORD
$previousWorldAccount = $env:EQ2_WORLD_ACCOUNT
$previousWorldPassword = $env:EQ2_WORLD_PASSWORD

$loginProcess = $null
$worldProcess = $null
$clientProcess = $null
$clientConfigUpdated = $false
$sessionWritten = $false
$sessionFailure = $null

try {
  if ($UpdateClientConfig) {
    Invoke-ClientConfigHelper `
      -Label "Set EQ2 client login address" `
      -Arguments @("-ConfigPath", $resolvedClientConfigPath, "-LoginHost", $ClientTargetHost)
    $clientConfigUpdated = $true
  }

  $loginDbPasswordSecret = Resolve-SecretOrConfig `
    -Value $DbPassword `
    -EnvName "EQ2_DB_PASSWORD" `
    -ParamName "-DbPassword" `
    -ConfigPath $LoginConfigPath `
    -ConfigSection "db" `
    -ConfigKey "password" `
    -ConfigLabel "db.password"

  if ($loginDbPasswordSecret.Source -ne "config") {
    $env:EQ2_DB_PASSWORD = $loginDbPasswordSecret.Value
  } else {
    $env:EQ2_DB_PASSWORD = $null
  }

  $loginProcess = Start-LoggedProcess `
    -Label "source2 login" `
    -Executable $loginServer `
    -Arguments $loginArgs `
    -StdOut $loginStdOut `
    -StdErr $loginStdErr

  if (!(Wait-TcpEndpoint -TargetHost $WorldLoginAddress -TargetPort $LoginPort -TimeoutMs 10000)) {
    throw "source2 login TCP $WorldLoginAddress`:$LoginPort did not become reachable for world registration."
  }

  $worldDbPasswordSecret = Resolve-SecretOrConfig `
    -Value $DbPassword `
    -EnvName "EQ2_DB_PASSWORD" `
    -ParamName "-DbPassword" `
    -ConfigPath $WorldConfigPath `
    -ConfigSection "world_db" `
    -ConfigKey "password" `
    -ConfigLabel "world_db.password"
  $worldAccountSecret = Resolve-SecretOrConfig `
    -Value $WorldAccount `
    -EnvName "EQ2_WORLD_ACCOUNT" `
    -ParamName "-WorldAccount" `
    -ConfigPath $WorldConfigPath `
    -ConfigSection "world" `
    -ConfigKey "account" `
    -ConfigLabel "world.account"
  $worldPasswordSecret = Resolve-SecretOrConfig `
    -Value $WorldPassword `
    -EnvName "EQ2_WORLD_PASSWORD" `
    -ParamName "-WorldPassword" `
    -ConfigPath $WorldConfigPath `
    -ConfigSection "world" `
    -ConfigKey "password" `
    -ConfigLabel "world.password"

  if ($worldDbPasswordSecret.Source -ne "config") {
    $env:EQ2_DB_PASSWORD = $worldDbPasswordSecret.Value
  } else {
    $env:EQ2_DB_PASSWORD = $null
  }
  if ($worldAccountSecret.Source -ne "config") {
    $env:EQ2_WORLD_ACCOUNT = $worldAccountSecret.Value
  } else {
    $env:EQ2_WORLD_ACCOUNT = $null
  }
  if ($worldPasswordSecret.Source -ne "config") {
    $env:EQ2_WORLD_PASSWORD = $worldPasswordSecret.Value
  } else {
    $env:EQ2_WORLD_PASSWORD = $null
  }

  $worldProcess = Start-LoggedProcess `
    -Label "source2 world" `
    -Executable $worldServer `
    -Arguments $worldArgs `
    -StdOut $worldStdOut `
    -StdErr $worldStdErr

  Start-Sleep -Seconds 2
  if ($loginProcess.HasExited) {
    throw "source2 login exited early with code $($loginProcess.ExitCode). See $loginStdOut and $loginStdErr."
  }
  if ($worldProcess.HasExited) {
    throw "source2 world exited early with code $($worldProcess.ExitCode). See $worldStdOut and $worldStdErr."
  }

  if ($LaunchClient) {
    if (!(Test-Path -LiteralPath $resolvedClientExePath)) {
      throw "Missing EQ2 client executable '$resolvedClientExePath'."
    }
    Write-Host "== Launching EQ2 client =="
    Write-Host (Format-CommandForDisplay -Executable $resolvedClientExePath -Arguments (Redact-ClientArgumentsForDisplay $ClientArguments))
    $startProcessParams = @{
      FilePath = $resolvedClientExePath
      WorkingDirectory = Split-Path -Parent $resolvedClientExePath
      PassThru = $true
    }
    if ($ClientArguments.Count -gt 0) {
      $startProcessParams.ArgumentList = ConvertTo-StartProcessArguments $ClientArguments
    }
    $clientProcess = Start-Process @startProcessParams
  }

  if ($AutoLoginClient) {
    Write-Host "== Auto-login EQ2 client =="
    $resolvedAutoLoginPassword = Resolve-AutoLoginValue `
      -Value $AutoLoginPassword `
      -EnvName "EQ2_LOGIN_PASSWORD" `
      -ParamName "-AutoLoginPassword"
    $resolvedAutoLoginUsername = ""
    if ($AutoLoginMode -eq "UsernamePassword") {
      $resolvedAutoLoginUsername = Resolve-AutoLoginValue `
        -Value $AutoLoginUsername `
        -EnvName "EQ2_LOGIN_USERNAME" `
        -ParamName "-AutoLoginUsername"
    }
    Invoke-ClientAutoLogin `
      -ClientProcess $clientProcess `
      -Mode $AutoLoginMode `
      -InputMethod $AutoLoginInputMethod `
      -TextMode $AutoLoginTextMode `
      -DelayMs $AutoLoginDelayMs `
      -Username $resolvedAutoLoginUsername `
      -Password $resolvedAutoLoginPassword `
      -CoordinateMode $AutoLoginCoordinateMode `
      -UsernameX $AutoLoginUsernameX `
      -UsernameY $AutoLoginUsernameY `
      -PasswordX $AutoLoginPasswordX `
      -PasswordY $AutoLoginPasswordY `
      -SubmitX $AutoLoginSubmitX `
      -SubmitY $AutoLoginSubmitY `
      -DiagnosticsPath $autoLoginDiagnosticsPath
    Write-Host "Auto-login keys sent."
  }

  Write-SessionReport `
    -Path $sessionReport `
    -StartedAt $startedAt `
    -LoginStdOut $loginStdOut `
    -LoginStdErr $loginStdErr `
    -WorldStdOut $worldStdOut `
    -WorldStdErr $worldStdErr `
    -LoginPid $loginProcess.Id `
    -WorldPid $worldProcess.Id `
    -ClientConfig $resolvedClientConfigPath `
    -ClientExe $resolvedClientExePath `
    -ClientPid $(if ($clientProcess) { "$($clientProcess.Id)" } else { "not launched" }) `
    -ClientArgumentsDisplay $(if ($ClientArguments.Count -gt 0) { (Redact-ClientArgumentsForDisplay $ClientArguments) -join " " } else { "none" }) `
    -ClientLogSnapshotDir $(if ($SnapshotClientLogs) { $clientLogSnapshotDir } else { "not requested" }) `
    -AutoLoginDiagnosticsPath $(if ($AutoLoginClient -and (Test-Path -LiteralPath $autoLoginDiagnosticsPath)) { $autoLoginDiagnosticsPath } else { "not requested" }) `
    -SessionStatus "ready"
  $sessionWritten = $true

  Write-Host "== Real-client acceptance session ready =="
  Write-Host "Set the EQ2 client to: cl_ls_address $ClientTargetHost"
  Write-Host "Session report: $sessionReport"
  Write-Host "Login logs: $loginStdOut ; $loginStdErr"
  Write-Host "World logs: $worldStdOut ; $worldStdErr"

  if ($NoWait) {
    if ($clientProcess) {
      Write-Host "Client launched pid=$($clientProcess.Id)"
    }
    if ($SnapshotClientLogs) {
      Write-Host "Client log snapshots are skipped with -NoWait; copy logs manually after the client run."
    }
    Write-Host "Processes left running. Stop them with: Stop-Process -Id $($loginProcess.Id),$($worldProcess.Id)"
    return
  }

  if ($RunForMs -gt 0) {
    Start-Sleep -Milliseconds $RunForMs
    return
  }

  Write-Host "Run the client gates now. Close the client before pressing Enter if you want complete client log snapshots."
  Write-Host "Press Enter here to stop source2."
  [void][Console]::ReadLine()
} catch {
  $sessionFailure = $_.Exception.Message
  throw
} finally {
  $env:EQ2_DB_PASSWORD = $previousDbPassword
  $env:EQ2_WORLD_ACCOUNT = $previousWorldAccount
  $env:EQ2_WORLD_PASSWORD = $previousWorldPassword

  if (!$NoWait) {
    Stop-Source2Process -Process $worldProcess -Label "source2 world"
    Stop-Source2Process -Process $loginProcess -Label "source2 login"
  }

  if ($SnapshotClientLogs -and !$NoWait) {
    try {
      if ($clientProcess -and !$clientProcess.HasExited) {
        Write-Host "Warning: EQ2 client pid=$($clientProcess.Id) is still running; client log snapshots may be partial."
      }
      Copy-ClientLogSnapshots -Paths $resolvedClientLogPaths -OutputDir $clientLogSnapshotDir
      Write-Host "Client log snapshots: $clientLogSnapshotDir"
    } catch {
      Write-Host "Warning: failed to snapshot client logs: $($_.Exception.Message)"
    }
  }

  if ($RestoreClientConfigOnExit -and $clientConfigUpdated -and !$NoWait) {
    try {
      Invoke-ClientConfigHelper `
        -Label "Restore EQ2 client login address" `
        -Arguments @("-ConfigPath", $resolvedClientConfigPath, "-RestoreOriginal")
    } catch {
      Write-Host "Warning: failed to restore EQ2 client config: $($_.Exception.Message)"
    }
  }

  if (!$sessionWritten -and !$NoWait) {
    try {
      Write-SessionReport `
        -Path $sessionReport `
        -StartedAt $startedAt `
        -LoginStdOut $loginStdOut `
        -LoginStdErr $loginStdErr `
        -WorldStdOut $worldStdOut `
        -WorldStdErr $worldStdErr `
        -LoginPid $(if ($loginProcess) { $loginProcess.Id } else { 0 }) `
        -WorldPid $(if ($worldProcess) { $worldProcess.Id } else { 0 }) `
        -ClientConfig $resolvedClientConfigPath `
        -ClientExe $resolvedClientExePath `
        -ClientPid $(if ($clientProcess) { "$($clientProcess.Id)" } else { "not launched" }) `
        -ClientArgumentsDisplay $(if ($ClientArguments.Count -gt 0) { (Redact-ClientArgumentsForDisplay $ClientArguments) -join " " } else { "none" }) `
        -ClientLogSnapshotDir $(if ($SnapshotClientLogs) { $clientLogSnapshotDir } else { "not requested" }) `
        -SessionStatus "failed before ready" `
        -FailureReason $(if ($sessionFailure) { $sessionFailure } else { "Session ended before the ready report was written." })
      Write-Host "Incomplete session report: $sessionReport"
    } catch {
      Write-Host "Warning: failed to write incomplete session report: $($_.Exception.Message)"
    }
  }
}
