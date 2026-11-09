//go:build windows

package screencast

import (
	"archive/zip"
	"bytes"
	"connect-installer.local/core/core"
	"connect-installer.local/core/products"
	"embed"
	"encoding/json"
	"fmt"
	"golang.org/x/sys/windows/registry"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"sync/atomic"
	"syscall"
)

//go:embed payload.zip ribbon.png
var assets embed.FS

const uninstallKey = `Software\Microsoft\Windows\CurrentVersion\Uninstall\Screencast`
const manifestName = "connect-installed-files.json"

func (a *Adapter) AfterRemoval() error {
	return core.CleanupUninstaller(a.dir, "Screencast-Uninstall.exe")
}

type manifest struct {
	Product string   `json:"product"`
	Files   []string `json:"files"`
}
type Adapter struct {
	dir       string
	progress  atomic.Int32
	installed bool
}

func New() *Adapter {
	dir := filepath.Join(os.Getenv("LOCALAPPDATA"), "Programs", "Screencast")
	if k, e := registry.OpenKey(registry.CURRENT_USER, uninstallKey, registry.QUERY_VALUE); e == nil {
		if p, _, e := k.GetStringValue("InstallLocation"); e == nil {
			dir = p
		}
		k.Close()
	}
	return &Adapter{dir: dir}
}
func (a *Adapter) Config() core.Config {
	return products.Configure("screencast", a.dir, assets)
}
func quote(s string) string { return "'" + strings.ReplaceAll(s, "'", "''") + "'" }
func runPS(script string) error {
	cmd := exec.Command("powershell.exe", "-NoProfile", "-NonInteractive", "-Command", "$ErrorActionPreference='Stop';"+script)
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true}
	out, e := cmd.CombinedOutput()
	if e != nil {
		return fmt.Errorf("%w: %s", e, out)
	}
	return nil
}
func (a *Adapter) Progress() core.Progress            { return core.Progress{Percent: int(a.progress.Load())} }
func (a *Adapter) Action(string, []byte) (any, error) { return nil, fmt.Errorf("unknown action") }
func (a *Adapter) Open() error {
	if !a.installed {
		return fmt.Errorf("installation not completed")
	}
	return exec.Command("explorer.exe", filepath.Join(a.dir, "Screencast.exe")).Start()
}

func (a *Adapter) Install(o core.Options) error {
	a.progress.Store(0)
	dir, err := core.ValidateDir(o.Path)
	if err != nil {
		return err
	}
	if _, err := os.Stat(filepath.Join(a.dir, "Screencast.exe")); err == nil && !strings.EqualFold(dir, a.dir) {
		return fmt.Errorf("update the existing installation at %s", a.dir)
	}
	if entries, e := os.ReadDir(dir); e == nil && len(entries) > 0 {
		if _, e := os.Stat(filepath.Join(dir, "Screencast.exe")); e != nil {
			return fmt.Errorf("choose an empty folder or existing Screencast installation")
		}
	}
	if err = core.CheckClosed(filepath.Join(dir, "Screencast.exe")); err != nil {
		return err
	}
	preferencesExist := false
	if k, e := registry.OpenKey(registry.CURRENT_USER, `Software\Screencast`, registry.QUERY_VALUE); e == nil {
		preferencesExist = true
		k.Close()
	} else if e != registry.ErrNotExist {
		return e
	}
	if _, e := os.Stat(filepath.Join(dir, "Screencast.exe")); e == nil {
		preferencesExist = true
	}
	raw, err := assets.ReadFile("payload.zip")
	if err != nil {
		return err
	}
	zr, err := zip.NewReader(bytes.NewReader(raw), int64(len(raw)))
	if err != nil {
		return err
	}
	stage, err := os.MkdirTemp("", "connect-screencast-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(stage)
	m := manifest{Product: "screencast"}
	seen := map[string]bool{}
	var total uint64
	for _, f := range zr.File {
		if f.FileInfo().IsDir() {
			continue
		}
		if f.Mode()&os.ModeSymlink != 0 {
			return fmt.Errorf("archive contains a link")
		}
		name := f.Name
		lower := strings.ToLower(filepath.Clean(filepath.FromSlash(name)))
		if seen[lower] {
			return fmt.Errorf("duplicate payload path")
		}
		seen[lower] = true
		if lower == manifestName || lower == "screencast-uninstall.exe" || lower == "installed-files.txt" {
			return fmt.Errorf("payload contains installer metadata")
		}
		target, e := core.OwnedPath(stage, name)
		if e != nil {
			return e
		}
		if _, e = core.OwnedPath(dir, name); e != nil {
			return e
		}
		total += f.UncompressedSize64
		if total > 4<<30 {
			return fmt.Errorf("payload too large")
		}
		if e = os.MkdirAll(filepath.Dir(target), 0755); e != nil {
			return e
		}
		in, e := f.Open()
		if e != nil {
			return e
		}
		out, e := os.OpenFile(target, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0644)
		if e != nil {
			in.Close()
			return e
		}
		_, e = io.Copy(out, in)
		closeErr := out.Close()
		in.Close()
		if e != nil {
			return e
		}
		if closeErr != nil {
			return closeErr
		}
		m.Files = append(m.Files, filepath.ToSlash(filepath.FromSlash(name)))
	}
	if !seen["screencast.exe"] {
		return fmt.Errorf("payload does not contain Screencast.exe")
	}
	a.progress.Store(25)
	// Check every replaced file before making any change. Stage full bytes first.
	for _, name := range m.Files {
		p, e := core.OwnedPath(dir, name)
		if e != nil {
			return e
		}
		if e = core.CheckClosed(p); e != nil {
			return e
		}
	}
	if err = os.MkdirAll(dir, 0755); err != nil {
		return err
	}
	for i, name := range m.Files {
		p, e := core.OwnedPath(dir, name)
		if e != nil {
			return e
		}
		if e = os.MkdirAll(filepath.Dir(p), 0755); e != nil {
			return e
		}
		data, e := os.ReadFile(filepath.Join(stage, filepath.FromSlash(name)))
		if e != nil {
			return e
		}
		tmp, e := os.CreateTemp(filepath.Dir(p), ".connect-*")
		if e != nil {
			return e
		}
		temp := tmp.Name()
		_, e = tmp.Write(data)
		if e == nil {
			e = tmp.Sync()
		}
		ce := tmp.Close()
		if e == nil {
			e = ce
		}
		if e == nil {
			e = os.Rename(temp, p)
		}
		os.Remove(temp)
		if e != nil {
			return e
		}
		a.progress.Store(int32(25 + (i+1)*45/len(m.Files)))
	}
	self, err := os.Executable()
	if err != nil {
		return err
	}
	data, err := os.ReadFile(self)
	if err != nil {
		return err
	}
	if err = os.WriteFile(filepath.Join(dir, "Screencast-Uninstall.exe"), data, 0755); err != nil {
		return err
	}
	// Manifest is derived from archive entries, never unrelated files already in the folder.
	m.Files = append(m.Files, "Screencast-Uninstall.exe")
	blob, _ := json.MarshalIndent(m, "", "  ")
	if err = os.WriteFile(filepath.Join(dir, manifestName), blob, 0644); err != nil {
		return err
	}
	a.dir = dir
	if err = a.shortcuts(o.Desktop, o.StartMenu); err != nil {
		return err
	}
	key, _, err := registry.CreateKey(registry.CURRENT_USER, uninstallKey, registry.SET_VALUE)
	if err != nil {
		return err
	}
	defer key.Close()
	for k, v := range map[string]string{"DisplayName": "Screencast", "DisplayVersion": products.Version("screencast"), "InstallLocation": dir, "DisplayIcon": filepath.Join(dir, "assets", "Screencast-shell-0.2.6.ico"), "UninstallString": `"` + filepath.Join(dir, "Screencast-Uninstall.exe") + `" --uninstall`} {
		if err = key.SetStringValue(k, v); err != nil {
			return err
		}
	}
	if !preferencesExist {
		k, _, e := registry.CreateKey(registry.CURRENT_USER, `Software\Screencast`, registry.SET_VALUE)
		if e != nil {
			return e
		}
		theme := uint32(1)
		if o.Theme == "dark" {
			theme = 2
		}
		english := uint32(0)
		if o.Language == "en" {
			english = 1
		}
		e = k.SetDWordValue("Theme", theme)
		if e == nil {
			e = k.SetDWordValue("English", english)
		}
		k.Close()
		if e != nil {
			return e
		}
	}
	a.installed = true
	a.progress.Store(100)
	return nil
}

func (a *Adapter) shortcuts(desktop, menu bool) error {
	script := `$ErrorActionPreference='Stop';$w=New-Object -ComObject WScript.Shell;$programs=[Environment]::GetFolderPath('Programs');foreach($p in @(($programs+'\Screencast\Screencast.lnk'),($programs+'\Screencast\Uninstall.lnk'))){if(Test-Path -LiteralPath $p){Remove-Item -LiteralPath $p}};$legacy=$programs+'\Screencast';if((Test-Path -LiteralPath $legacy) -and !(Get-ChildItem -LiteralPath $legacy -Force)){Remove-Item -LiteralPath $legacy};`
	app := filepath.Join(a.dir, "Screencast.exe")
	icon := filepath.Join(a.dir, "assets", "Screencast-shell-0.2.6.ico")
	for _, s := range []struct {
		enabled            bool
		path, target, args string
	}{
		{desktop, `[Environment]::GetFolderPath('Desktop')+ '\Screencast.lnk'`, app, ""},
		{menu, `[Environment]::GetFolderPath('Programs')+ '\Screencast.lnk'`, app, ""},
	} {
		if s.enabled {
			script += `$p=` + s.path + `;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($p))|Out-Null;$l=$w.CreateShortcut($p);$l.TargetPath=` + quote(s.target) + `;$l.Arguments=` + quote(s.args) + `;$l.WorkingDirectory=` + quote(a.dir) + `;$l.IconLocation=` + quote(icon+",0") + `;$l.Save();`
        } else {
            script += `$p=` + s.path + `;if(Test-Path -LiteralPath $p){Remove-Item -LiteralPath $p};`
        }
	}
	return runPS(script)
}

func (a *Adapter) Remove() error {
	dir, err := core.ValidateDir(a.dir)
	if err != nil {
		return err
	}
	if err = core.CheckClosed(filepath.Join(dir, "Screencast.exe")); err != nil {
		return err
	}
	raw, err := os.ReadFile(filepath.Join(dir, manifestName))
	if err != nil {
		return fmt.Errorf("ownership manifest missing; legacy installer must perform removal: %w", err)
	}
	var m manifest
	if err = json.Unmarshal(raw, &m); err != nil || m.Product != "screencast" {
		return fmt.Errorf("invalid ownership manifest")
	}
	// Validate the complete manifest and links before deleting anything.
	paths := []string{}
	self, _ := os.Executable()
	for _, name := range m.Files {
		p, e := core.OwnedPath(dir, name)
		if e != nil {
			return e
		}
		if strings.EqualFold(p, self) {
			continue
		}
		if e = core.CheckClosed(p); e != nil {
			return e
		}
		paths = append(paths, p)
	}
	sort.Sort(sort.Reverse(sort.StringSlice(paths)))
	for _, p := range paths {
		if e := os.Remove(p); e != nil && !os.IsNotExist(e) {
			return e
		}
	}
	if err = registry.DeleteKey(registry.CURRENT_USER, uninstallKey); err != nil && err != registry.ErrNotExist {
		return err
	}
	// Settings and user-created files remain. Empty directories only are removed.
	if err = runPS(`foreach($p in @(([Environment]::GetFolderPath('Desktop')+'\Screencast.lnk'),([Environment]::GetFolderPath('Programs')+'\Screencast.lnk'),([Environment]::GetFolderPath('Programs')+'\Screencast\Screencast.lnk'),([Environment]::GetFolderPath('Programs')+'\Screencast\Uninstall.lnk'))){if(Test-Path -LiteralPath $p){Remove-Item -LiteralPath $p}};$legacy=[Environment]::GetFolderPath('Programs')+'\Screencast';if((Test-Path -LiteralPath $legacy) -and !(Get-ChildItem -LiteralPath $legacy -Force)){Remove-Item -LiteralPath $legacy}`); err != nil {
		return err
	}
	if err = os.Remove(filepath.Join(dir, manifestName)); err != nil {
		return err
	}
	a.progress.Store(100)
	return nil
}
