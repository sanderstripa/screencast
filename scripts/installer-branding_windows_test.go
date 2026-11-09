//go:build windows

package screencast

import (
 "bytes"
 "connect-installer.local/core/core"
 "connect-installer.local/core/internal/adaptertest"
 "golang.org/x/sys/windows/registry"
 "os"
 "path/filepath"
 "testing"
)

type checkedBranding struct { core.Adapter; t *testing.T; installs *int }

func (a *checkedBranding) Install(o core.Options) error {
 if err := a.Adapter.Install(o); err != nil { return err }
 canonical, err := os.ReadFile(filepath.Join(o.Path,"assets","Screencast.ico")); if err != nil { a.t.Fatal(err) }
 aliases,err := filepath.Glob(filepath.Join(o.Path,"assets","Screencast-shell-*.ico")); if err != nil || len(aliases)<5 { a.t.Fatal("legacy aliases missing",err) }
 for _,p := range aliases { b,e:=os.ReadFile(p);if e!=nil || !bytes.Equal(b,canonical){a.t.Fatal("stale installed icon",p,e)} }
 key,e:=registry.OpenKey(registry.CURRENT_USER,uninstallKey,registry.QUERY_VALUE);if e!=nil{a.t.Fatal(e)}
 icon,_,e:=key.GetStringValue("DisplayIcon");key.Close();if e!=nil{a.t.Fatal(e)}
 data,e:=os.ReadFile(icon);if e!=nil || !bytes.Equal(data,canonical){a.t.Fatal("registered icon differs",e)}
 script:=`$ErrorActionPreference='Stop';Add-Type -TypeDefinition 'using System;using System.Text;using System.Runtime.InteropServices;public static class ScreencastShortcutPath{[DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]public static extern uint GetLongPathName(string p,StringBuilder b,uint n);}';function LongPath($p){$b=New-Object Text.StringBuilder 32768;if([ScreencastShortcutPath]::GetLongPathName($p,$b,32768)-eq0){throw ('Cannot resolve shortcut target: '+$p)};return $b.ToString()};$w=New-Object -ComObject WScript.Shell;$programs=[Environment]::GetFolderPath('Programs');foreach($p in @(([Environment]::GetFolderPath('Desktop')+'\Screencast.lnk'),($programs+'\Screencast.lnk'))){if(!(Test-Path -LiteralPath $p)){throw 'Installed shortcut missing'};$l=$w.CreateShortcut($p);if($l.IconLocation -ne `+quote(icon+",0")+`){throw ('Stale installed shortcut icon: '+$l.IconLocation)}};if(Test-Path -LiteralPath ($programs+'\Screencast')){throw 'Legacy Start Menu folder remains'};if((LongPath ($w.CreateShortcut($programs+'\Screencast.lnk').TargetPath)) -ne (LongPath `+quote(filepath.Join(o.Path,"Screencast.exe"))+`)){throw 'Wrong Start Menu shortcut target'}`
 if e=runPS(script);e!=nil{a.t.Fatal(e)}
 (*a.installs)++
 if *a.installs==1 {
  if e=runPS(`$p=[Environment]::GetFolderPath('Programs')+'\Screencast';[IO.Directory]::CreateDirectory($p)|Out-Null;$w=New-Object -ComObject WScript.Shell;foreach($n in @('Screencast.lnk','Uninstall.lnk')){$l=$w.CreateShortcut($p+'\'+$n);$l.TargetPath=`+quote(filepath.Join(o.Path,"Screencast.exe"))+`;$l.Save()}`);e!=nil{a.t.Fatal(e)}
  if e=os.WriteFile(filepath.Join(o.Path,"assets","Screencast-shell-0.2.5.ico"),[]byte("stale icon from previous installation"),0644);e!=nil{a.t.Fatal(e)} }
 return nil
}

func (a *checkedBranding) Remove() error {
 if err:=a.Adapter.Remove();err!=nil{return err}
 if err:=runPS(`if(Test-Path -LiteralPath ([Environment]::GetFolderPath('Programs')+'\Screencast.lnk')){throw 'Start shortcut remains after uninstall'};if(Test-Path -LiteralPath ([Environment]::GetFolderPath('Programs')+'\Screencast')){throw 'Start folder remains after uninstall'}`);err!=nil{a.t.Fatal(err)}
 return nil
}

func TestStartMenuRemoval(t *testing.T) {
 // Lifecycle below verifies this against the actual Windows shell, including an old folder on upgrade.
 if os.Getenv("CONNECT_INSTALLER_CI_TEST")!="1" {t.Skip("isolated CI only")}
 a:=New()
 if e:=a.shortcuts(false,false);e!=nil{t.Fatal(e)}
 if e:=runPS(`if(Test-Path -LiteralPath ([Environment]::GetFolderPath('Programs')+'\Screencast.lnk')){throw 'Start shortcut remains after removal'}`);e!=nil{t.Fatal(e)}
}

func TestInstalledBrandingAndLegacyIconUpgrade(t *testing.T) {
 count:=0
 adaptertest.Lifecycle(t,"screencast","Screencast.exe",func() core.Adapter{return &checkedBranding{Adapter:New(),t:t,installs:&count}})
}
