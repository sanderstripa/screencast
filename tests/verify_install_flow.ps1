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
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")] static extern IntPtr GetStyle(IntPtr h,int index);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] public static extern IntPtr SetText(IntPtr h,uint m,IntPtr w,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,StringBuilder text);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 public static string Text(IntPtr h){var s=new StringBuilder(32768);ReadText(h,0xD,(IntPtr)32768,s);return s.ToString();}
 public static bool Shown(IntPtr h){return (GetStyle(h,-16).ToInt64()&0x10000000)!=0;}
 public static IntPtr Find(uint process,string name){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);var s=new StringBuilder(128);GetClassName(h,s,128);if(owner==process&&s.ToString()==name){result=h;return false;}return true;},IntPtr.Zero);return result;}
}
'@
function Wait-For([scriptblock]$Condition,[string]$Failure,[int]$Seconds=45){$end=[DateTime]::UtcNow.AddSeconds($Seconds);do{if(& $Condition){return};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow-lt$end);throw $Failure}
function Click([IntPtr]$Window,[int]$Id){[void][InstallFlow]::SendMessage($Window,0x111,[IntPtr]$Id,[IntPtr]::Zero)}
New-Item 'HKCU:\Software\Screencast' -Force > $null
New-ItemProperty 'HKCU:\Software\Screencast' -Name SaveFolder -Value (Join-Path $Directory 'preserved-recordings') -PropertyType String -Force > $null
foreach($attempt in 1..4){
 $process=Start-Process -FilePath $Setup -WindowStyle Hidden -PassThru
 $script:flowWindow=[IntPtr]::Zero
 try{
  Wait-For {$script:flowWindow=[InstallFlow]::Find($process.Id,'ScreencastSetup');$script:flowWindow-ne[IntPtr]::Zero} 'Installer did not open'
  Click $script:flowWindow 3
  Wait-For {[InstallFlow]::GetDlgItem($script:flowWindow,9)-ne[IntPtr]::Zero} 'Installation path field absent'
  [void][InstallFlow]::SetText([InstallFlow]::GetDlgItem($script:flowWindow,9),0xC,[IntPtr]::Zero,$target)
  Wait-For {[InstallFlow]::Text([InstallFlow]::GetDlgItem($script:flowWindow,9))-eq$target} 'Edited destination did not reach the real native path field'
  $theme=if($attempt-le2){1}else{2};$english=if($attempt%2-eq1){1}else{0}
  Click $script:flowWindow $(if($english){12}else{1})
  Click $script:flowWindow $(if($theme-eq1){2}else{13})
  if(-not[InstallFlow]::Shown([InstallFlow]::GetDlgItem($script:flowWindow,9))){throw 'Theme/language change left installation settings'}
  Click $script:flowWindow 5;Click $script:flowWindow 6;Click $script:flowWindow 4
  Wait-For {$button=[InstallFlow]::GetDlgItem($script:flowWindow,4);[InstallFlow]::Shown($button)-and[InstallFlow]::Text($button)-in @('Готово','Done')} 'Real installation did not finish'
  $registration=Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Screencast'
  if($registration.DisplayVersion-ne'0.3.8'-or$registration.InstallLocation-ne$target){throw "Installer registration/version/path mismatch: actual=$($registration.InstallLocation), expected=$target, version=$($registration.DisplayVersion)"}
  $preferences=Get-ItemProperty 'HKCU:\Software\Screencast'
  if($preferences.Theme-ne$theme-or$preferences.English-ne$english){throw 'Installer did not persist selected application appearance'}
  if($preferences.SaveFolder-ne(Join-Path $Directory 'preserved-recordings')){throw 'Installer overwrote unrelated application preferences'}
  foreach($file in @('Screencast.exe','Screencast-Uninstall.exe','assets/Montserrat.ttf','installed-files.txt')){if(-not(Test-Path -LiteralPath (Join-Path $target $file))){throw "Missing installed file: $file"}}
  Click $script:flowWindow 7;Click $script:flowWindow 4
  if(-not $process.WaitForExit(15000)-or$process.ExitCode-ne0){throw 'Installer completion failed'}
  $testOutput=Join-Path $Directory "installed-app-check-$attempt"
  $app=Start-Process -FilePath (Join-Path $target 'Screencast.exe') -ArgumentList '--ui-test',('"'+$testOutput+'"'),$theme,$english -WindowStyle Hidden -PassThru
  if(-not$app.WaitForExit(30000)-or$app.ExitCode-ne0){throw 'Installed application did not load selected theme/language or pass UI check'}
  Get-Content (Join-Path $testOutput 'startup-preferences.txt')
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
'PASS: real installation/reinstall in all four theme/language combinations; installed-app startup and UI changes; unrelated preferences preserved; registered version/path; actual uninstall/self-removal.'
