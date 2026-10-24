# ZZOpenLara Music Wizard for Windows PowerShell 5.1+
# Copyright (C) 2026 Xanxi. See accompanying licence notices.
# This wrapper does not require Python on the user's computer.
# It runs the unmodified ZZOpenLara-Music.exe with BOTH disc inputs at once.

$ErrorActionPreference = 'Stop'
$temporary = $null
try {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.Application]::EnableVisualStyles()

    $folder = Split-Path -Parent $MyInvocation.MyCommand.Path
    $exe = Join-Path $folder 'ZZOpenLara-Music.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw 'ZZOpenLara-Music.exe is missing from the tools folder.'
    }

    function Pick-Cue([string]$title, [string]$startingFolder) {
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Title = $title
        $dialog.Filter = 'CUE disc images (*.cue)|*.cue'
        $dialog.CheckFileExists = $true
        $dialog.CheckPathExists = $true
        $dialog.Multiselect = $false
        $dialog.RestoreDirectory = $true
        if ($startingFolder -and (Test-Path -LiteralPath $startingFolder -PathType Container)) {
            $dialog.InitialDirectory = $startingFolder
        }
        try {
            if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return $null }
            return $dialog.FileName
        } finally {
            $dialog.Dispose()
        }
    }

    Write-Host ''
    Write-Host 'ZZOpenLara - FULL MUSIC WIZARD (58 tracks)' -ForegroundColor Cyan
    Write-Host 'Step 1/2: choose the Tomb Raider 1 PLAYSTATION disc .CUE file.'
    $psx = Pick-Cue 'Step 1/2 - Select the Tomb Raider I PLAYSTATION CUE' ''
    if (-not $psx) { Write-Host 'Cancelled. Nothing was changed.'; exit 2 }
    Write-Host ('PlayStation: ' + $psx)

    Write-Host ''
    Write-Host 'Step 2/2: choose the Tomb Raider 1 PC audio source.'
    $kind = [System.Windows.Forms.MessageBox]::Show(
        "Choose the PC music source:`n`nYES = PC CD .CUE file (recommended)`nNO = Folder containing track02.wav, track03.wav, etc.`nCANCEL = Stop without changing anything",
        'ZZOpenLara - Step 2/2 - PC CD audio',
        [System.Windows.Forms.MessageBoxButtons]::YesNoCancel,
        [System.Windows.Forms.MessageBoxIcon]::Question)
    $pc = $null
    $pcFolder = $null
    if ($kind -eq [System.Windows.Forms.DialogResult]::Yes) {
        $pc = Pick-Cue 'Step 2/2 - Select the Tomb Raider I PC CD CUE' ''
        if (-not $pc) { Write-Host 'Cancelled. Nothing was changed.'; exit 2 }
        if ([string]::Equals($psx, $pc, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw 'The PlayStation and PC CUE files must be different.'
        }
        Write-Host ('PC CUE: ' + $pc)
    } elseif ($kind -eq [System.Windows.Forms.DialogResult]::No) {
        $pickFolder = New-Object System.Windows.Forms.FolderBrowserDialog
        $pickFolder.Description = 'Select the folder containing PC CD tracks (track02.wav, track04.wav, track05.wav, etc.)'
        $pickFolder.ShowNewFolderButton = $false
        try {
            if ($pickFolder.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
                Write-Host 'Cancelled. Nothing was changed.'; exit 2
            }
            $pcFolder = $pickFolder.SelectedPath
        } finally {
            $pickFolder.Dispose()
        }
        if (-not ((Get-ChildItem -LiteralPath $pcFolder -Filter 'track*.wav' -File -ErrorAction SilentlyContinue).Count -gt 0)) {
            throw 'The selected folder contains no trackNN.wav files.'
        }
        Write-Host ('PC WAV folder: ' + $pcFolder)
    } else {
        Write-Host 'Cancelled. Nothing was changed.'
        exit 2
    }

    $confirm = [System.Windows.Forms.MessageBox]::Show(
        "Ready to combine both music sources in ONE conversion?`n`nPSX: $psx`n`nPC: $(if ($pc) { $pc } else { $pcFolder })`n`nThe existing tools\ol_music.pak will NOT be touched.",
        'ZZOpenLara - Confirm complete soundtrack',
        [System.Windows.Forms.MessageBoxButtons]::OKCancel,
        [System.Windows.Forms.MessageBoxIcon]::Information)
    if ($confirm -ne [System.Windows.Forms.DialogResult]::OK) {
        Write-Host 'Cancelled. Nothing was changed.'; exit 2
    }

    # The complete conversion has a separate output directory.  Never overwrite
    # a file created by the PSX-only drag-and-drop tool or the user's Amiga data.
    $outputDirectory = Join-Path $folder 'complete_music'
    if (-not (Test-Path -LiteralPath $outputDirectory)) {
        New-Item -Path $outputDirectory -ItemType Directory -Force | Out-Null
    }
    $temporary = Join-Path $outputDirectory ('ol_music_BUILDING_' + [guid]::NewGuid().ToString('N') + '.pak')
    Write-Host ''
    Write-Host 'Converting the PSX and PC discs together. This may take a while.' -ForegroundColor Cyan
    if ($pc) {
        & $exe --psx $psx --pc $pc --out $temporary
    } else {
        & $exe --psx $psx --pc-wav $pcFolder --out $temporary
    }
    $code = $LASTEXITCODE
    if ($code -ne 0) { throw ('The audio converter failed (exit code ' + $code + ').') }
    if (-not (Test-Path -LiteralPath $temporary -PathType Leaf)) {
        throw 'The converter reported success but produced no output PAK.'
    }

    # Validate the Quake PAK directory and all 58 named music entries before
    # moving the new result to its final name.  Incomplete PSX-only PAKs fail.
    $reader = $null
    try {
        $stream = [System.IO.File]::OpenRead($temporary)
        $reader = [System.IO.BinaryReader]::new($stream)
        $magic = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
        if ($magic -ne 'PACK') { throw 'Invalid music archive: PACK header not found.' }
        $dirPosition = $reader.ReadInt32()
        $dirLength = $reader.ReadInt32()
        if ($dirPosition -lt 12 -or $dirLength -le 0 -or ($dirLength % 64) -ne 0 -or
            ([long]$dirPosition + [long]$dirLength) -gt $stream.Length) {
            throw 'Invalid music archive directory.'
        }
        $reader.BaseStream.Position = $dirPosition
        $count = [int]($dirLength / 64)
        $tracks = [System.Collections.Generic.HashSet[int]]::new()
        for ($i = 0; $i -lt $count; $i++) {
            $name = [System.Text.Encoding]::ASCII.GetString($reader.ReadBytes(56)).Split([char]0)[0]
            $offset = $reader.ReadInt32()
            $length = $reader.ReadInt32()
            if ($name -match '^track_(\d{2})\.zzm$') {
                $trackNumber = [int]$Matches[1]
            } else {
                throw ('Invalid PAK directory name: ' + $name)
            }
            if ($offset -lt 0 -or $length -lt 16 -or
                ([long]$offset + [long]$length) -gt $stream.Length) {
                throw ('Invalid PAK entry bounds: ' + $name)
            }
            if (-not $tracks.Add($trackNumber)) {
                throw ('Duplicate music track: ' + $name)
            }
        }
        if ($tracks.Count -ne 58) { throw ('Incomplete soundtrack: found ' + $tracks.Count + ' tracks, expected 58.') }
        for ($n = 2; $n -le 59; $n++) {
            if (-not $tracks.Contains($n)) {
                throw ('Incomplete soundtrack: missing track ' + $n + '.')
            }
        }
    } finally {
        if ($reader) { $reader.Dispose() }
    }

    $final = Join-Path $outputDirectory 'ol_music.pak'
    if (Test-Path -LiteralPath $final) {
        $backup = Join-Path $outputDirectory ('ol_music_previous_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' + [guid]::NewGuid().ToString('N').Substring(0, 6) + '.pak')
        Move-Item -LiteralPath $final -Destination $backup -ErrorAction Stop
        Write-Host ('Previous complete PAK saved as: ' + $backup)
    }
    Move-Item -LiteralPath $temporary -Destination $final -ErrorAction Stop
    $temporary = $null
    $bytes = (Get-Item -LiteralPath $final).Length
    Write-Host ''
    Write-Host ('SUCCESS: 58 music tracks (02-59), including Wind and Pyramid.') -ForegroundColor Green
    Write-Host ('File size: ' + $bytes + ' bytes')
    Write-Host ('New file: ' + $final)
    Write-Host 'Copy THIS file to ZZOpenLara/data/ on your Amiga after comparing it.'
    [System.Windows.Forms.MessageBox]::Show(
        "Success: 58 tracks, including Wind and Pyramid.`n`nFile saved as:`n$final`n`nSize: $bytes bytes`n`nCopy to ZZOpenLara\data\ on the Amiga AFTER comparing it.",
        'ZZOpenLara - Complete soundtrack ready',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information) | Out-Null
    exit 0
} catch {
    Write-Host ''
    Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'Any existing ol_music.pak was left untouched.'
    try {
        [System.Windows.Forms.MessageBox]::Show(
            ('Music conversion failed:' + [Environment]::NewLine + $_.Exception.Message + [Environment]::NewLine + [Environment]::NewLine + 'Existing music files were not changed.'),
            'ZZOpenLara - Error',
            [System.Windows.Forms.MessageBoxButtons]::OK,
            [System.Windows.Forms.MessageBoxIcon]::Error) | Out-Null
    } catch { }
    exit 1
} finally {
    if ($temporary -and (Test-Path -LiteralPath $temporary)) {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}
