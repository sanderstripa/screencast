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
 script:=`$w=New-Object -ComObject WScript.Shell;foreach($p in @(([Environment]::GetFolderPath('Desktop')+'\Screencast.lnk'),([Environment]::GetFolderPath('Programs')+'\Screencast\Screencast.lnk'),([Environment]::GetFolderPath('Programs')+'\Screencast\Uninstall.lnk'))){if(!(Test-Path -LiteralPath $p)){throw 'Installed shortcut missing'};$l=$w.CreateShortcut($p);if($l.IconLocation -ne `+quote(icon+",0")+`){throw ('Stale installed shortcut icon: '+$l.IconLocation)}}`
 if e=runPS(script);e!=nil{a.t.Fatal(e)}
 (*a.installs)++
 if *a.installs==1 { if e=os.WriteFile(filepath.Join(o.Path,"assets","Screencast-shell-0.2.5.ico"),[]byte("stale icon from previous installation"),0644);e!=nil{a.t.Fatal(e)} }
 return nil
}

func TestInstalledBrandingAndLegacyIconUpgrade(t *testing.T) {
 count:=0
 adaptertest.Lifecycle(t,"screencast","Screencast.exe",func() core.Adapter{return &checkedBranding{Adapter:New(),t:t,installs:&count}})
}
