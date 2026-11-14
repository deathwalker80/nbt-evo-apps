# dme-lettura - READ-ONLY look at what the engine computer (DME) of your car answers, from a Windows PC with an
# ENET cable in the OBD socket. Nothing is written to the car and nothing is installed on the head unit.
#
# What it sends, and nothing else:
#   - one UDP "who is there" to 169.254.255.255:6811 (vehicle identification, as E-Sys / ISTA): it finds the gateway;
#     the answer contains the VIN, which is NOT written anywhere;
#   - to the DME only (address 0x12), one request per connection, 0.2 s apart:
#       UDS 22 xx xx  ReadDataByIdentifier (read)      OBD 01 xx  current data (read)
#     the script refuses any other request: no session change, no writing, no routine, no clearing of faults.
# The list of identifiers is the reference list of BoostDeck (B58 engine, 2016 340i: MIT, github.com/inotbarnacle/
# ID6_apps_prerelease, docs/SIGNALS.md) plus standard OBD values: on a MINI engine some will be refused or mean
# something else. That is what this test is for.
#
# Car: parked, handbrake, ignition on (engine running gives live values; engine off gives the values at rest).
# Output: on screen and in dme-lettura.txt on the Desktop (full, nothing cut). Keep it for us: it is your car's data.
#
# Run (PowerShell): powershell -ExecutionPolicy Bypass -Command "iex (Get-Content -Raw '<path of this file>')"

$ErrorActionPreference = 'Stop'
$Locale = ''          # computer's ENET address 169.254.x.x ('' = found by itself)
$Gateway = ''         # gateway address 169.254.x.x ('' = found by itself)
$Dme = 0x12
$Richieste = @(
    # BMW identifiers of BoostDeck (B58), reference conversions
    '224205', '224201', '225889', '224A49', '22580F', '2256D7', '22586F', '224300',
    # standard OBD: supported lists, then values
    '0100', '0120', '0140',
    '0104', '0105', '010B', '010C', '010D', '010E', '010F', '0111', '0123', '0133', '015C'
)

$Cartella = [Environment]::GetFolderPath('Desktop')
if (-not $Cartella) { $Cartella = (Get-Location).Path }
$File = Join-Path $Cartella 'dme-lettura.txt'

function Scrivi([string]$t) {
    $r = '{0:yyyy-MM-dd HH:mm:ss.fff} {1}' -f (Get-Date), $t
    Write-Host $r
    Add-Content -Path $File -Value $r -Encoding UTF8
}

function Esa([byte[]]$b) {
    if ($null -eq $b -or $b.Length -eq 0) { return '' }
    return (($b | ForEach-Object { '{0:X2}' -f $_ }) -join ' ')
}

function IndirizziEnet {
    $out = @()
    foreach ($ni in [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces()) {
        foreach ($u in $ni.GetIPProperties().UnicastAddresses) {
            $a = $u.Address
            if ($a.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) { continue }
            $b = $a.GetAddressBytes()
            if ($b[0] -eq 169 -and $b[1] -eq 254 -and $b[2] -ne 0 -and $b[2] -ne 255) { $out += $a.ToString() }
        }
    }
    return $out
}

function CercaGateway([string]$loc) {
    $u = New-Object System.Net.Sockets.UdpClient (New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Parse($loc)), 0)
    try {
        $u.EnableBroadcast = $true
        $u.Client.ReceiveTimeout = 2000
        $q = [byte[]](0, 0, 0, 0, 0, 0x11)
        [void]$u.Send($q, $q.Length, (New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Parse('169.254.255.255')), 6811))
        $fine = (Get-Date).AddSeconds(2)
        while ((Get-Date) -lt $fine) {
            $ep = New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Any, 0)
            try { $b = $u.Receive([ref]$ep) } catch { break }
            if ($b.Length -lt 6 -or $b[4] -ne 0 -or $b[5] -ne 0x11) { continue }
            $txt = [System.Text.Encoding]::ASCII.GetString($b, 6, $b.Length - 6)
            if ($txt -match 'DIAGADR10') { return $ep.Address.ToString() }      # 10 = gateway (62/63 = head units)
        }
    } finally { $u.Close() }
    return ''
}

function LeggiEsatti($s, [int]$n) {
    $buf = New-Object byte[] $n
    $got = 0
    while ($got -lt $n) {
        try { $k = $s.Read($buf, $got, $n - $got) } catch { return $null }
        if ($k -le 0) { return $null }
        $got += $k
    }
    return ,$buf
}

function Chiedi([string]$gw, [string]$loc, [int]$ecu, [byte[]]$req) {
    $c = New-Object System.Net.Sockets.TcpClient (New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Parse($loc)), 0)
    try {
        $iar = $c.BeginConnect($gw, 6801, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(2000)) { return 'ERROR: no connection to the gateway' }
        $c.EndConnect($iar)
        $c.ReceiveTimeout = 2000
        $c.SendTimeout = 2000
        $s = $c.GetStream()
        $len = $req.Length + 2
        $frame = [byte[]](@((($len -shr 24) -band 255), (($len -shr 16) -band 255), (($len -shr 8) -band 255), ($len -band 255), 0, 1, 0xF4, $ecu) + $req)
        $s.Write($frame, 0, $frame.Length)
        $fine = (Get-Date).AddSeconds(3)
        while ((Get-Date) -lt $fine) {
            $h = LeggiEsatti $s 6
            if ($null -eq $h) { return 'ERROR: no answer' }
            $l = ([int]$h[0] -shl 24) -bor ([int]$h[1] -shl 16) -bor ([int]$h[2] -shl 8) -bor [int]$h[3]
            $ctrl = ([int]$h[4] -shl 8) -bor [int]$h[5]
            if ($l -gt 8192 -or $l -lt 0) { return "ERROR: frame of $l bytes" }
            $p = LeggiEsatti $s $l
            if ($null -eq $p) { return 'ERROR: answer cut' }
            if ($ctrl -eq 0x12) {                                  # alive check of the gateway: answer it
                $a = [byte[]](0, 0, 0, 2, 0, 0x13, 0, 0xF4)
                $s.Write($a, 0, $a.Length)
                continue
            }
            if ($ctrl -ne 1 -or $l -lt 3 -or $p[0] -ne $ecu -or $p[1] -ne 0xF4) { continue }   # ack echo and others
            $r = [byte[]]$p[2..($p.Length - 1)]
            if ($r[0] -eq ($req[0] + 0x40)) {
                $ok = $true
                for ($i = 1; $i -lt $req.Length; $i++) { if ($r.Length -le $i -or $r[$i] -ne $req[$i]) { $ok = $false } }
                if ($ok) { return ,([byte[]]$r) }
            }
            if ($r.Length -eq 3 -and $r[0] -eq 0x7F -and $r[1] -eq $req[0]) {
                if ($r[2] -eq 0x78) { $fine = (Get-Date).AddSeconds(3); continue }   # "wait": the answer comes later
                return ('REFUSED: NRC {0:X2}' -f $r[2])
            }
        }
        return 'ERROR: no matching answer'
    } finally { $c.Close() }
}

function U16([byte[]]$d) { return ([int]$d[0] -shl 8) -bor [int]$d[1] }

# reference conversions (B58 for the BMW identifiers: to be checked on this engine)
function Leggibile([string]$q, [byte[]]$d) {
    try {
        switch ($q) {
            '224205' { return ('pressione collettore {0:N1} kPa (B58: raw/128)' -f ((U16 $d) / 128)) }
            '224201' { return ('pressione ambiente {0:N1} kPa (B58: raw/256)' -f ((U16 $d) / 256)) }
            '225889' { return ('lambda {0:N3} (B58: raw*16/65535)' -f ((U16 $d) * 16 / 65535)) }
            '224A49' { $v = U16 $d; if ($v -ge 32768) { $v -= 65536 }; return ('anticipo {0:N1} gradi (B58: signed/10)' -f ($v / 10)) }
            '22580F' { return ('aria aspirata {0:N1} C (B58: raw*0.75-48)' -f ($d[0] * 0.75 - 48)) }
            '2256D7' { return ('pressione rail {0:N1} bar (B58: raw/50)' -f ((U16 $d) / 50)) }
            '22586F' { return ('pressione olio {0:N2} bar (B58: raw/1000)' -f ((U16 $d) / 1000)) }
            '224300' { return ('liquido raffreddamento {0:N1} C (B58: raw/10-273.15)' -f ((U16 $d) / 10 - 273.15)) }
            '0104' { return ('carico {0:N0} %' -f ($d[0] * 100 / 255)) }
            '0105' { return ('liquido raffreddamento {0} C' -f ($d[0] - 40)) }
            '010B' { return ('pressione collettore {0} kPa' -f $d[0]) }
            '010C' { return ('giri {0:N0}' -f ((U16 $d) / 4)) }
            '010D' { return ('velocita {0} km/h' -f $d[0]) }
            '010E' { return ('anticipo {0:N1} gradi' -f ($d[0] / 2 - 64)) }
            '010F' { return ('aria aspirata {0} C' -f ($d[0] - 40)) }
            '0111' { return ('farfalla {0:N0} %' -f ($d[0] * 100 / 255)) }
            '0123' { return ('pressione rail {0:N0} kPa' -f ((U16 $d) * 10)) }
            '0133' { return ('pressione ambiente {0} kPa' -f $d[0]) }
            '015C' { return ('olio {0} C' -f ($d[0] - 40)) }
            { $_ -in '0100', '0120', '0140' } {
                $base = [Convert]::ToInt32($q.Substring(2, 2), 16)
                $si = @()
                for ($i = 0; $i -lt 32; $i++) { if ($d[[int][Math]::Floor($i / 8)] -band (0x80 -shr ($i % 8))) { $si += ('{0:X2}' -f ($base + $i + 1)) } }
                return ('OBD supportati: ' + ($si -join ' '))
            }
            default { return '' }
        }
    } catch { return '(conversione non possibile)' }
}

Scrivi '=== dme-lettura (sola lettura) ==='
if (-not $Locale) {
    $tutti = @(IndirizziEnet)
    if ($tutti.Count -eq 0) { Scrivi 'ERROR: no 169.254.x.x network card: is the ENET cable in, with the ignition on?'; return }
    $Locale = $tutti[0]
}
Scrivi "computer: $Locale"
if (-not $Gateway) { $Gateway = CercaGateway $Locale }
if (-not $Gateway) {
    Scrivi 'the gateway did not answer the search (ignition on? firewall of Windows?)'
    $Gateway = (Read-Host 'gateway address 169.254.x.x if you know it (Enter = stop)').Trim()
    if ($Gateway -notmatch '^169\.254\.\d{1,3}\.\d{1,3}$') { Scrivi 'stopped: no gateway'; return }
}
Scrivi "gateway: $Gateway   DME: 0x$('{0:X2}' -f $Dme)"
foreach ($q in $Richieste) {
    if ($q -notmatch '^(22[0-9A-Fa-f]{4}|01[0-9A-Fa-f]{2})$') { Scrivi "SKIPPED (not a read request): $q"; continue }
    $req = [byte[]]@(for ($i = 0; $i -lt $q.Length; $i += 2) { [Convert]::ToByte($q.Substring($i, 2), 16) })
    $r = Chiedi $Gateway $Locale $Dme $req
    if ($r -is [byte[]]) {
        $dati = [byte[]]@()
        if ($r.Length -gt $req.Length) { $dati = [byte[]]$r[$req.Length..($r.Length - 1)] }
        Scrivi ('{0}  ->  {1}   dati: {2}   {3}' -f (Esa $req), (Esa $r), (Esa $dati), (Leggibile $q.ToUpper() $dati))
    } else {
        Scrivi ('{0}  ->  {1}' -f (Esa $req), $r)
    }
    Start-Sleep -Milliseconds 200
}
Scrivi "=== fine: $File ==="
