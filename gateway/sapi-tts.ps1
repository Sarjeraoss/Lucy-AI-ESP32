param([Parameter(Mandatory=$true)][string]$EncodedText,[Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Speech
$text = [System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($EncodedText))
if ([string]::IsNullOrWhiteSpace($text) -or $text.Length -gt 180) { throw 'Invalid speech text' }
$format = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)
$voice = New-Object System.Speech.Synthesis.SpeechSynthesizer
try { $voice.Rate=0; $voice.Volume=100; $voice.SetOutputToWaveFile($OutputFile,$format); $voice.Speak($text) }
finally { $voice.Dispose() }
