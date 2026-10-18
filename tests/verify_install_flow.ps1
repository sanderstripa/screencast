param([string]$Setup,[string]$Directory)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'Full installation flow is restricted to the isolated CI runner.'}
$Setup=(Resolve-Path -LiteralPath $Setup).Path
$target=[IO.Path]::GetFullPath((Join-Path $Directory 'Screencast'))
if(Test-Path -LiteralPath $target){throw 'Full install test requires a new dedicated directory'}
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class InstallFlow {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f,IntPtr p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static string Text(IntPtr h){var s=new StringBuilder(1024);GetWindowText(h,s,1024);return s.ToString();}
 public static IntPtr Find(uint process,string name){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);var s=new StringBuilder(128);GetClassName(h,s,128);if(owner==process&&s.ToString()==name){result=h;return false;}return true;},IntPtr.Zero);return result;}
}
'@
function Wait-For([scriptblock]$Condition,[string]$Failure,[int]$Seconds=45){$end=[DateTime]::UtcNow.AddSeconds($Seconds);do{if(& $Condition){return};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end);throw $Failure}
function Click([IntPtr]$Window,[int]$Id){[void][InstallFlow]::SendMessage($Window,0x111,[IntPtr]$Id,[IntPtr]::Zero)}
foreach($attempt in 1..2){
 $process=Start-Process -FilePath $Setup -WindowStyle Hidden -PassThru
 $script:flowWindow=[IntPtr]::Zero
 try{
  Wait-For {$script:flowWindow=[InstallFlow]::Find($process.Id,'ScreencastSetup');$script:flowWindow-ne[IntPtr]::Zero} 'Installer did not open'
  Click $script:flowWindow 3
  Wait-For {[InstallFlow]::GetDlgItem($script:flowWindow,9)-ne[IntPtr]::Zero} 'Installation path field absent'
  [void][InstallFlow]::SetWindowText([InstallFlow]::GetDlgItem($script:flowWindow,9),$target)
  Click $script:flowWindow 5;Click $script:flowWindow 6;Click $script:flowWindow 4
  Wait-For {[InstallFlow]::Text([InstallFlow]::GetDlgItem($script:flowWindow,4))-in @('Готово','Done')} 'Real installation did not finish'
  foreach($file in @('Screencast.exe','Screencast-Uninstall.exe','assets/Montserrat.ttf','installed-files.txt')){if(-not(Test-Path -LiteralPath (Join-Path $target $file))){throw "Missing installed file: $file"}}
  $registration=Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Screencast'
  if($registration.DisplayVersion-ne'0.3.7'-or$registration.InstallLocation-ne$target){throw 'Installer registration/version/path mismatch'}
  Click $script:flowWindow 7;Click $script:flowWindow 4
  if(-not $process.WaitForExit(15000)-or$process.ExitCode-ne0){throw 'Installer completion failed'}
 }finally{if(-not$process.HasExited){$process.Kill()}}
}
$testOutput=Join-Path $Directory 'installed-app-check'
$app=Start-Process -FilePath (Join-Path $target 'Screencast.exe') -ArgumentList '--ui-test',('"'+$testOutput+'"') -WindowStyle Hidden -PassThru
if(-not$app.WaitForExit(30000)-or$app.ExitCode-ne0){throw 'Installed application did not pass UI check'}
$uninstall=Start-Process -FilePath (Join-Path $target 'Screencast-Uninstall.exe') -ArgumentList '--uninstall' -WindowStyle Hidden -PassThru
try{
 $script:confirmation=[IntPtr]::Zero
 Wait-For {$script:confirmation=[InstallFlow]::Find($uninstall.Id,'ScreencastNotice');$script:confirmation-ne[IntPtr]::Zero} 'Uninstall confirmation did not appear'
 Click $script:confirmation 1
 if(-not$uninstall.WaitForExit(30000)-or$uninstall.ExitCode-ne0){throw 'Actual uninstaller failed'}
 Wait-For {-not(Test-Path -LiteralPath (Join-Path $target 'Screencast-Uninstall.exe'))} 'Uninstaller self-removal did not finish' 20
 if(Test-Path -LiteralPath (Join-Path $target 'Screencast.exe')){throw 'Uninstaller left the application behind'}
 if(Test-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Screencast'){throw 'Uninstaller left its registration behind'}
}finally{if(-not$uninstall.HasExited){$uninstall.Kill()}}
'PASS: real installation, same-version reinstall, registered version/path, installed-app UI, confirmation and actual uninstall/self-removal.'
