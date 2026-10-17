[CmdletBinding()]
param(
    [Parameter(Mandatory)][uri]$GatewayUrl,
    [string]$OutputPath,
    [ValidateRange(1, 300)][int]$TimeoutSeconds = 60
)

# Read-only Gateway capture. No configuration, run, fault-ack or update commands.
$ErrorActionPreference = 'Stop'
if ($GatewayUrl.Scheme -notin @('http', 'https')) { throw 'GatewayUrl must be an HTTP or HTTPS URL.' }
$socket = [System.Net.WebSockets.ClientWebSocket]::new()
$deadline = [System.Threading.CancellationTokenSource]::new($TimeoutSeconds * 1000)
$capture = [ordered]@{
    captured_at = [DateTime]::UtcNow.ToString('o')
    completed_at = $null
    gateway = $null
    stm32 = $null
    status = $null
    status_observed_at = $null
    configuration = $null
    reports = @()
}
$requestNumber = 0

function Receive-Message {
    $buffer = [byte[]]::new(65536)
    $message = [Text.StringBuilder]::new()
    do {
        $part = $socket.ReceiveAsync([ArraySegment[byte]]::new($buffer), $deadline.Token).GetAwaiter().GetResult()
        if ($part.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) { throw 'Gateway closed the connection.' }
        [void]$message.Append([Text.Encoding]::UTF8.GetString($buffer, 0, $part.Count))
    } while (-not $part.EndOfMessage)
    $value = $message.ToString() | ConvertFrom-Json
    if ($value.type -eq 'hello') {
        $capture.gateway = [ordered]@{ version = $value.gateway_version; build_id = $value.gateway_build_id; uart_state = $value.gateway_status.uart_state; uptime_ms = $value.gateway_status.gateway_uptime_ms; partition = $value.gateway_status.gateway_partition }
    }
    if ($value.type -eq 'bms_status' -or $value.type -eq 'snapshot') {
        $capture.status = $value.status
        $capture.status_observed_at = [DateTime]::UtcNow.ToString('o')
    }
    return $value
}

function Read-Service([string]$Name, [hashtable]$Arguments) {
    if ($Name -notin @('get_config', 'get_device_info', 'get_diagnostic_report')) { throw 'Only diagnostic reads are allowed.' }
    for ($attempt = 0; $attempt -lt 4; ++$attempt) {
        $script:requestNumber++
        $id = "diagnose-$script:requestNumber"
        $json = @{ v = 1; type = 'service'; request_id = $id; service = $Name; arguments = $Arguments } | ConvertTo-Json -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($json)
        [void]$socket.SendAsync([ArraySegment[byte]]::new($bytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $deadline.Token).GetAwaiter().GetResult()
        do { $message = Receive-Message } while ($message.type -ne 'service_result' -or $message.request_id -ne $id)
        if ($message.result -ne 'busy') { return $message }
        Start-Sleep -Milliseconds 250
    }
    return $message
}

try {
    $address = [UriBuilder]::new($GatewayUrl)
    $address.Scheme = if ($GatewayUrl.Scheme -eq 'https') { 'wss' } else { 'ws' }
    $address.Path = '/ws'
    [void]$socket.ConnectAsync($address.Uri, $deadline.Token).GetAwaiter().GetResult()
    while ($null -eq $capture.status) { $null = Receive-Message }
    $capture.configuration = Read-Service 'get_config' @{}
    $capture.stm32 = Read-Service 'get_device_info' @{}
    for ($index = 0; $index -lt $capture.status.slave_count; ++$index) {
        $capture.reports += Read-Service 'get_diagnostic_report' @{ slave_index = $index }
    }
    $capture.completed_at = [DateTime]::UtcNow.ToString('o')
    $json = $capture | ConvertTo-Json -Depth 12
    if ($OutputPath) {
        $resolvedOutputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
        [IO.File]::WriteAllText($resolvedOutputPath, $json, [Text.UTF8Encoding]::new($false))
    }
    $json
}
finally {
    $socket.Abort()
    $socket.Dispose()
    $deadline.Dispose()
}
