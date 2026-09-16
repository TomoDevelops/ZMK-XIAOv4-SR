[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Left,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Right,

    [string]$Output = (Join-Path "split-logs" (Get-Date -Format "yyyyMMdd-HHmmss"))
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path $Output).Path

$utf8 = [System.Text.UTF8Encoding]::new($false)
$combinedWriter = [System.IO.StreamWriter]::new(
    (Join-Path $Output "combined.log"), $true, $utf8
)
$combinedWriter.AutoFlush = $true

function New-CaptureState {
    param([string]$Side, [string]$PortName)

    $writer = [System.IO.StreamWriter]::new(
        (Join-Path $Output "$Side.log"), $true, $utf8
    )
    $writer.AutoFlush = $true

    [pscustomobject]@{
        Side      = $Side
        PortName  = $PortName
        Port      = $null
        Buffer    = ""
        Writer    = $writer
        LastRetry = [datetime]::MinValue
    }
}

function Write-CaptureLine {
    param($State, [string]$Message)

    $stamp = [DateTimeOffset]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")
    $line = "[$stamp] [$($State.Side)] $($Message.TrimEnd())"
    $State.Writer.WriteLine($line)
    $combinedWriter.WriteLine($line)
    Write-Host $line
}

function Write-Marker {
    $stamp = [DateTimeOffset]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")
    $line = "[$stamp] [MARK] lag observed"
    $combinedWriter.WriteLine($line)
    Write-Host $line -ForegroundColor Yellow
}

function Close-CapturePort {
    param($State)

    if ($null -ne $State.Port) {
        try { $State.Port.Close() } catch {}
        try { $State.Port.Dispose() } catch {}
        $State.Port = $null
    }
}

function Open-CapturePort {
    param($State)

    try {
        $port = [System.IO.Ports.SerialPort]::new(
            $State.PortName,
            115200,
            [System.IO.Ports.Parity]::None,
            8,
            [System.IO.Ports.StopBits]::One
        )
        $port.DtrEnable = $true
        $port.RtsEnable = $false
        $port.ReadTimeout = 100
        $State.Port = $port
        $port.Open()
        $State.Buffer = ""
        Write-CaptureLine $State "CAPTURE_OPEN port=$($State.PortName)"
    }
    catch {
        $message = $_.Exception.Message
        Close-CapturePort $State
        $now = Get-Date
        if (($now - $State.LastRetry).TotalSeconds -ge 5) {
            $State.LastRetry = $now
            Write-CaptureLine $State "CAPTURE_RETRY port=$($State.PortName) error=$message"
        }
    }
}

function Read-CapturePort {
    param($State)

    if ($null -eq $State.Port) {
        Open-CapturePort $State
        return
    }

    try {
        if (-not $State.Port.IsOpen) {
            throw "serial port closed"
        }

        $chunk = $State.Port.ReadExisting()
        if ([string]::IsNullOrEmpty($chunk)) {
            return
        }

        $State.Buffer += $chunk
        while (($newline = $State.Buffer.IndexOf("`n")) -ge 0) {
            $line = $State.Buffer.Substring(0, $newline).TrimEnd([char]13)
            $State.Buffer = $State.Buffer.Substring($newline + 1)
            Write-CaptureLine $State $line
        }
    }
    catch {
        Write-CaptureLine $State "CAPTURE_DISCONNECTED port=$($State.PortName) error=$($_.Exception.Message)"
        Close-CapturePort $State
    }
}

$states = @(
    (New-CaptureState "left" $Left),
    (New-CaptureState "right" $Right)
)

Write-Host "Capturing to $Output"
Write-Host "Press M immediately after lag to add a timestamped marker; press Q to stop."

try {
    $running = $true
    while ($running) {
        foreach ($state in $states) {
            Read-CapturePort $state
        }

        if (-not [Console]::IsInputRedirected -and [Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            switch ($key.Key) {
                "M" { Write-Marker }
                "Q" { $running = $false }
            }
        }

        Start-Sleep -Milliseconds 25
    }
}
finally {
    foreach ($state in $states) {
        Close-CapturePort $state
        $state.Writer.Dispose()
    }
    $combinedWriter.Dispose()
}
