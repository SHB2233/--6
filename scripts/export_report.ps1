param([string]$Docx, [string]$Pdf)
$ErrorActionPreference='Stop'
if (!$Docx) { $Docx=Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'delivery\report\实验6_Socket通信编程实验报告.docx' }
if (!$Pdf) { $Pdf=[IO.Path]::ChangeExtension($Docx,'.pdf') }
$word=$null; $document=$null
try {
    $word=New-Object -ComObject Word.Application
    $word.Visible=$false; $word.DisplayAlerts=0
    $document=$word.Documents.Open($Docx,$false,$true)
    $document.ExportAsFixedFormat($Pdf,17)
    Write-Output ('PDF exported with '+$word.Name+' '+$word.Version+': '+$Pdf)
} finally {
    if ($document) { $document.Close(0); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($document) }
    if ($word) { $word.Quit(); [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($word) }
}
