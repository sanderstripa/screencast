param([string]$App,[string]$Setup,[string]$Shortcuts,[switch]$IconsOnly)
$ErrorActionPreference='Stop'
$App=(Resolve-Path -LiteralPath $App).Path;$Setup=(Resolve-Path -LiteralPath $Setup).Path;$Shortcuts=(Resolve-Path -LiteralPath $Shortcuts).Path
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class ShellCheck {
 [StructLayout(LayoutKind.Sequential)] public struct Size {public int cx,cy;public Size(int s){cx=s;cy=s;}}
 [StructLayout(LayoutKind.Sequential)] struct Bitmap {public int type,width,height,stride;public ushort planes,bpp;public IntPtr bits;}
 [StructLayout(LayoutKind.Sequential)] struct Info {public uint size;public int width,height;public ushort planes,bpp;public uint compression,imageSize;public int x,y;public uint used,important;}
 [StructLayout(LayoutKind.Sequential)] struct IconInfo {public bool icon;public uint x,y;public IntPtr mask,color;}
 [ComImport,Guid("bcc18b79-ba16-442f-80c4-8a59c30c463b"),InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] interface Factory {
  [PreserveSig] int GetImage(Size size,uint flags,out IntPtr image);
 }
 [DllImport("shell32.dll",CharSet=CharSet.Unicode)] static extern int SHCreateItemFromParsingName(string path,IntPtr context,ref Guid iid,out Factory factory);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr LoadLibraryEx(string path,IntPtr file,uint flags);
 [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr LoadIcon(IntPtr module,IntPtr name);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr LoadImage(IntPtr module,string name,uint type,int w,int h,uint flags);
 [DllImport("user32.dll")] static extern bool GetIconInfo(IntPtr icon,out IconInfo info);
 [DllImport("user32.dll")] static extern bool DestroyIcon(IntPtr icon);
 [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr value);
 [DllImport("gdi32.dll")] static extern int GetObject(IntPtr value,int size,out Bitmap bitmap);
 [DllImport("gdi32.dll")] static extern int GetDIBits(IntPtr dc,IntPtr bitmap,uint first,uint lines,byte[] bytes,ref Info info,uint use);
 [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr window);
 [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window,IntPtr dc);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window,int command);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
 [DllImport("user32.dll")] public static extern bool SystemParametersInfo(uint action,uint parameter,out Rect rect,uint flags);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
 delegate bool Enumerate(IntPtr window,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumWindows(Enumerate callback,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder name,int count);
 static byte[] Read(IntPtr handle,out int width,out int height) {
  Bitmap bitmap;if(GetObject(handle,Marshal.SizeOf<Bitmap>(),out bitmap)==0)throw new Exception("Missing bitmap");width=bitmap.width;height=Math.Abs(bitmap.height);
  Info info=new Info{size=40,width=width,height=-height,planes=1,bpp=32};byte[] bytes=new byte[width*height*4];IntPtr dc=GetDC(IntPtr.Zero);
  try {if(GetDIBits(dc,handle,0,(uint)height,bytes,ref info,0)==0)throw new Exception("Cannot read icon pixels");}finally{ReleaseDC(IntPtr.Zero,dc);}return bytes;
 }
 public static byte[] Icon(IntPtr handle,out int width,out int height) {
  IconInfo info;if(!GetIconInfo(handle,out info))throw new Exception("Missing icon");
  try{return Read(info.color,out width,out height);}finally{DeleteObject(info.color);DeleteObject(info.mask);}
 }
 [DllImport("shell32.dll",CharSet=CharSet.Unicode)] static extern uint ExtractIconEx(string path,int index,out IntPtr large,out IntPtr small,uint count);
 public static byte[] Resource(string path,out int width,out int height) {
  IntPtr large,small;if(ExtractIconEx(path,0,out large,out small,1)==0)throw new Exception("Cannot load icon resource");
  try{return Icon(large,out width,out height);}finally{DestroyIcon(large);DestroyIcon(small);}
 }
 public static void Shortcut(string path,string iconPath,int size) {
  Guid iid=typeof(Factory).GUID;Factory factory;Marshal.ThrowExceptionForHR(SHCreateItemFromParsingName(path,IntPtr.Zero,ref iid,out factory));IntPtr image;
  try{Marshal.ThrowExceptionForHR(factory.GetImage(new Size(size),4,out image));}finally{Marshal.ReleaseComObject(factory);}
  // Explorer's image cache uses standard 16/32/48/etc sizes and resamples 20/24,
  // even when exact ICO entries exist. Compare these two sizes through the same
  // shell renderer; canonical sizes must still match the native ICO entry exactly.
  bool cachedSize=size==20 || size==24;
  IntPtr expected;
  if(cachedSize){Factory source;Marshal.ThrowExceptionForHR(SHCreateItemFromParsingName(iconPath,IntPtr.Zero,ref iid,out source));try{Marshal.ThrowExceptionForHR(source.GetImage(new Size(size),4,out expected));}finally{Marshal.ReleaseComObject(source);}}
  else expected=LoadImage(IntPtr.Zero,iconPath,1,size,size,16);
  try {
   int w,h,ew,eh;byte[] pixels=Read(image,out w,out h),reference=cachedSize?Read(expected,out ew,out eh):Icon(expected,out ew,out eh);
   if(w!=size || h!=size || ew!=size || eh!=size)throw new Exception("Shortcut returned a small bitmap");
   long error=0,count=0;for(int y=0;y<h;y++)for(int x=0;x<w;x++){
    int p=(y*w+x)*4;if(reference[p+3]<250 || (x<size/3 && y>size*2/3))continue;
    for(int c=0;c<3;c++){error+=Math.Abs(pixels[p+c]-reference[p+c]);count++;}
   }
   if(count==0 || (double)error/count>2.0)throw new Exception("Shortcut icon was resampled from a low-resolution source: "+((double)error/count));
  }finally{DeleteObject(image);if(cachedSize)DeleteObject(expected);else DestroyIcon(expected);}
 }
 public static IntPtr[] Windows() {
  List<IntPtr> windows=new List<IntPtr>();EnumWindows((window,data)=>{StringBuilder name=new StringBuilder(128);GetClassName(window,name,128);if(name.ToString()=="ScreencastWindow")windows.Add(window);return true;},IntPtr.Zero);return windows.ToArray();
 }
}
'@
$shell=New-Object -ComObject WScript.Shell
$icon=Join-Path (Split-Path -Parent $App) 'assets/Screencast-shell-0.4.3.ico'
foreach($file in @('Screencast.lnk','Uninstall.lnk')) {
 $path=Join-Path $Shortcuts $file;$link=$shell.CreateShortcut($path)
 if($link.IconLocation -ne "$icon,0"){throw "Shortcut uses another icon: $($link.IconLocation)"}
 foreach($size in @(16,20,24,32,48,64,128,256)){[ShellCheck]::Shortcut($path,$icon,$size)}
}
$w=0;$h=0;$setupPixels=[ShellCheck]::Resource($Setup,[ref]$w,[ref]$h)
if($w-ne32-or$h-ne32){throw 'Installer icon is not 32x32'}
$appPixels=[ShellCheck]::Resource($App,[ref]$w,[ref]$h)
if([Convert]::ToBase64String($setupPixels)-ne[Convert]::ToBase64String($appPixels)){throw 'App and installer resource icons differ'}
if($IconsOnly){'Shortcut icons and app/installer resource images match.';return}
$launchers=@();foreach($folder in @('Desktop','StartMenu')) {
 $directory=Join-Path $Shortcuts $folder;New-Item -ItemType Directory -Path $directory -Force|Out-Null
 $launcher=Join-Path $directory 'Screencast.lnk';Copy-Item -LiteralPath (Join-Path $Shortcuts 'Screencast.lnk') -Destination $launcher -Force;$launchers+=$launcher
}
$primary=Start-Process -FilePath $App -WindowStyle Hidden -PassThru
try {
 $deadline=[DateTime]::UtcNow.AddSeconds(15);$window=[IntPtr]::Zero
 while([DateTime]::UtcNow-lt$deadline){foreach($candidate in [ShellCheck]::Windows()){[uint32]$ownerPid=0;[ShellCheck]::GetWindowThreadProcessId($candidate,[ref]$ownerPid)|Out-Null;if($ownerPid-eq$primary.Id){$window=$candidate;break}};if($window-ne[IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
 if($window-eq[IntPtr]::Zero){throw 'Main window did not start'}
 foreach($kind in @(0,1,2)) {
  $handle=[ShellCheck]::SendMessage($window,0x7f,[IntPtr]$kind,[IntPtr]96);$pixels=[ShellCheck]::Icon($handle,[ref]$w,[ref]$h)
  if($w-ne32-or$h-ne32-or[Convert]::ToBase64String($pixels)-ne[Convert]::ToBase64String($setupPixels)){throw 'A window icon request differs from the installer'}
 }
 # Exercise ordinary shortcuts through the same shell path as desktop/Start.
 foreach($launcher in $launchers) {foreach($attempt in 1..3) {
  [ShellCheck]::ShowWindow($window,6)|Out-Null
  $duplicate=Start-Process -FilePath $launcher -WindowStyle Hidden -PassThru
  if($duplicate-and-not$duplicate.WaitForExit(10000)){throw 'Secondary instance did not exit'}
  $windows=@([ShellCheck]::Windows())
  if($windows.Count-ne1-or$windows[0]-ne$window-or[ShellCheck]::IsIconic($window)-or-not[ShellCheck]::IsWindowVisible($window)){throw 'Repeat launch opened another window or did not restore the first'}
 }}
 $workArea=New-Object ShellCheck+Rect;$beforeMove=New-Object ShellCheck+Rect
 [ShellCheck]::SystemParametersInfo(0x30,0,[ref]$workArea,0)|Out-Null
 [ShellCheck]::GetWindowRect($window,[ref]$beforeMove)|Out-Null
 $moveX=[Math]::Max($workArea.left,[Math]::Min($workArea.left+20,$workArea.right-($beforeMove.right-$beforeMove.left)))
 $moveY=[Math]::Max($workArea.top,[Math]::Min($workArea.top+20,$workArea.bottom-($beforeMove.bottom-$beforeMove.top)))
 [ShellCheck]::SetWindowPos($window,[IntPtr]::Zero,$moveX,$moveY,0,0,0x15)|Out-Null
 [ShellCheck]::SendMessage($window,0x232,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 $savedPosition=New-Object ShellCheck+Rect
 [ShellCheck]::GetWindowRect($window,[ref]$savedPosition)|Out-Null
 'Shortcuts: native 32/48/64/128 icons; all window icon requests match installer; six repeat launches restore one window.'
}finally {
 if($window-ne[IntPtr]::Zero){[ShellCheck]::SendMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
 if(-not$primary.WaitForExit(15000)){$primary.Kill();throw 'Test application did not close'}
}
# Actual new processes must restore placement, including after closing minimized.
foreach($minimizeBeforeClose in @($true,$false)) {
 $reopened=Start-Process -FilePath $App -WindowStyle Hidden -PassThru
 $window=[IntPtr]::Zero
 try {
  $deadline=[DateTime]::UtcNow.AddSeconds(15)
  while([DateTime]::UtcNow-lt$deadline){foreach($candidate in [ShellCheck]::Windows()){[uint32]$ownerPid=0;[ShellCheck]::GetWindowThreadProcessId($candidate,[ref]$ownerPid)|Out-Null;if($ownerPid-eq$reopened.Id){$window=$candidate;break}};if($window-ne[IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
  if($window-eq[IntPtr]::Zero){throw 'Position restart did not open a window'}
  [ShellCheck]::SendMessage($window,0,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  $actual=New-Object ShellCheck+Rect
  $deadline=[DateTime]::UtcNow.AddSeconds(5)
  do {
   [ShellCheck]::GetWindowRect($window,[ref]$actual)|Out-Null
   $matches=$actual.left-eq$savedPosition.left-and$actual.top-eq$savedPosition.top-and$actual.right-eq$savedPosition.right-and$actual.bottom-eq$savedPosition.bottom
   if($matches){break};Start-Sleep -Milliseconds 50
  } while([DateTime]::UtcNow-lt$deadline)
  foreach($field in @('left','top','right','bottom')){if($actual.$field-ne$savedPosition.$field){throw "Window position/size changed across process restart: $field"}}
  if($minimizeBeforeClose){[ShellCheck]::ShowWindow($window,6)|Out-Null}
 } finally {
  if($window-ne[IntPtr]::Zero){[ShellCheck]::SendMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
  if(-not$reopened.WaitForExit(15000)){$reopened.Kill();throw 'Reopened app did not close'}
 }
}
'Window placement: exact position and unchanged size restored in two fresh processes, including after minimized close.'
