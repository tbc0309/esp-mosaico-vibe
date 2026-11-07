# Export only built-in Microsoft Windows/.NET timezone data. No external package.
param([string]$Output = "$PSScriptRoot/../resources/timezones/windows-source.json")
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class WindowsZonesExport {
 [DllImport("icu.dll", CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ucal_openTimeZoneIDEnumeration(int kind,IntPtr region,IntPtr offset,ref int error);
 [DllImport("icu.dll", CallingConvention=CallingConvention.Cdecl)] static extern IntPtr uenum_next(IntPtr e,ref int length,ref int error);
 [DllImport("icu.dll", CallingConvention=CallingConvention.Cdecl)] static extern void uenum_close(IntPtr e);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr LoadLibraryEx(string file,IntPtr handle,uint flags);
 [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr handle);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int LoadString(IntPtr handle,uint id,StringBuilder text,int length);
 public static Dictionary<string,string> Aliases() {
  var map=new Dictionary<string,string>();int error=0,length=0;var e=ucal_openTimeZoneIDEnumeration(0,IntPtr.Zero,IntPtr.Zero,ref error);
  if(error>0||e==IntPtr.Zero)throw new Exception("Windows ICU enumeration failed");
  try {for(;;){var p=uenum_next(e,ref length,ref error);if(p==IntPtr.Zero)break;var name=Marshal.PtrToStringAnsi(p,length);string id;if(TimeZoneInfo.TryConvertIanaIdToWindowsId(name,out id))map[name]=id;}}
  finally {uenum_close(e);}return map;
 }
 public static List<long[]> Transitions(TimeZoneInfo zone) {
  long start=1577836800,end=4133980800; // 2020-01-01 through 2101-01-01 UTC
  Func<long,long> offset=t=>(long)zone.GetUtcOffset(DateTimeOffset.FromUnixTimeSeconds(t)).TotalSeconds;
  long previous=offset(start);var rows=new List<long[]>{new long[]{start,previous}};
  for(long t=start+21600;t<=end;t+=21600){long next=offset(t);if(next==previous)continue;
   long lo=t-21600,hi=t;while(hi-lo>1){long m=(lo+hi)/2;if(offset(m)==previous)lo=m;else hi=m;}
   rows.Add(new long[]{hi,next});previous=next;
  }return rows;
 }
 public static string Label(string locale,uint id) {
  string path=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),locale,"tzres.dll.mui");
  var h=LoadLibraryEx(path,IntPtr.Zero,2);if(h==IntPtr.Zero)return "";
  try{var text=new StringBuilder(512);return LoadString(h,id,text,text.Capacity)>0?text.ToString():"";}finally{FreeLibrary(h);}
 }
}
'@
$zones = @()
$locales = [ordered]@{zh='zh-CN';'zh-TW'='zh-TW';en='en-US';fr='fr-FR';de='de-DE';ru='ru-RU';ja='ja-JP';ko='ko-KR'}
foreach ($zone in [TimeZoneInfo]::GetSystemTimeZones()) {
 $key = Get-ItemProperty -LiteralPath "HKLM:/SOFTWARE/Microsoft/Windows NT/CurrentVersion/Time Zones/$($zone.Id)"
 $resourceId = [uint32]([regex]::Match($key.MUI_Display,',-(\d+)$').Groups[1].Value)
 $labels = [ordered]@{}
 foreach($locale in $locales.Keys){$label=[WindowsZonesExport]::Label($locales[$locale],$resourceId);if($label){$labels[$locale]=$label}}
 if(!$labels['zh']){$labels['zh']=$zone.DisplayName}
 if(!$labels['en']){$labels['en']=$zone.Id}
 $zones += [ordered]@{id=$zone.Id;offset=[int]$zone.BaseUtcOffset.TotalSeconds;labels=$labels;transitions=@([WindowsZonesExport]::Transitions($zone))}
}
$version=Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows NT/CurrentVersion'
$record=[ordered]@{source='Microsoft Windows registry / System.TimeZoneInfo';build="$($version.CurrentBuild).$($version.UBR)";fromEpoch=1577836800;untilEpoch=4133980800;zones=$zones;aliases=[WindowsZonesExport]::Aliases()}
$record | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Output "Exported $($zones.Count) Windows timezones from build $($record.build)"
