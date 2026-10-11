param([string]$Run, [string]$Out)
Add-Type -AssemblyName System.Drawing
$proto = 'C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui'
$map = [ordered]@{
 's02_idle'='02_idle'; 's03_hover_free'='03_hover_free'; 's04_midstroke'='04_midstroke'; 's05_snap_end'='05_snap_end'; 's06_snap_road'='06_snap_road';
 's07_snap_extension_guide'='07_snap_extension_guide'; 's08_snap_offset_guide'='08_snap_parallel_offset_guide'; 's08b_snap_offset_guide_other'='08b_snap_offset_guide_3x';
 's09_hover_free_second'='09_hover_free_second'; 's10_snap_angle_length'='10_snap_angle_length'; 's11_shift_snap_off'='11_shift_snap_off'; 's12_error_min_span'='12_error_min_span';
 's13_error_too_tight'='13_error_too_tight'; 's14_advisory_radius'='14_advisory_radius'; 's15_advisory_grade'='15_advisory_grade'; 's16_advisory_bridge'='16_advisory_bridge';
 's17_advisory_tunnel'='17_advisory_tunnel'; 's18_type3_dual_ghost'='18_type3_dual_ghost'; 's19_type4_motorway_ghost'='19_type4_motorway_ghost';
 's20_type3_single_carriageway_O'='20_type3_single_carriageway_O'; 's21_type1_oneway'='21_type1_oneway'; 's22_type2_b_road_ghost'='22_type2_b_road_ghost';
 's26_stroke_3pts_before_finish'='26_stroke_3pts_before_finish'; 's27_finished_road_idle'='27_finished_road_idle'; 's28_error_crossing_too_close'='28_error_crossing_too_close' }
$codec = [Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | ? { $_.MimeType -eq 'image/jpeg' }
$ep = New-Object Drawing.Imaging.EncoderParameters(1); $ep.Param[0] = New-Object Drawing.Imaging.EncoderParameter([Drawing.Imaging.Encoder]::Quality, [long]80)
foreach ($k in $map.Keys) {
  $a = Join-Path $Run ("20_st_$k.png"); $b = Join-Path $proto ($map[$k] + '.png')
  if (!(Test-Path $a) -or !(Test-Path $b)) { Write-Host "missing $k"; continue }
  $ia = [Drawing.Image]::FromFile($a); $ib = [Drawing.Image]::FromFile($b)
  $w = 800; $ha = [int]($ia.Height * $w / $ia.Width); $hb = [int]($ib.Height * $w / $ib.Width)
  $bmp = New-Object Drawing.Bitmap (2*$w), ([Math]::Max($ha,$hb))
  $g = [Drawing.Graphics]::FromImage($bmp); $g.InterpolationMode = 'HighQualityBicubic'; $g.Clear([Drawing.Color]::Black)
  $g.DrawImage($ia, 0, 0, $w, $ha); $g.DrawImage($ib, $w, 0, $w, $hb); $g.Dispose()
  $bmp.Save((Join-Path $Out ($k + '_vs_proto.jpg')), $codec, $ep); $bmp.Dispose(); $ia.Dispose(); $ib.Dispose()
}
