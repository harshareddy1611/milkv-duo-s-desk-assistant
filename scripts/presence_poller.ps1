<#
.SYNOPSIS
  Polls the Duo S over SSH for presence_daemon's state and locks the
  workstation when the desk goes "away". Run this continuously on the PC.

.NOTES
  - Requires passwordless SSH key auth to root@$BoardIp already set up.
  - Does NOT attempt to unlock on "present" - Windows requires real
    authentication to unlock, and that's the point of a lock screen.
  - Break reminders show as a Windows toast-style balloon notification.
#>

param(
    [string]$BoardIp = "192.168.42.1",
    [int]$PollIntervalSeconds = 4
)

$ErrorActionPreference = "SilentlyContinue"
$lastState = $null

function Show-Notification {
    param([string]$Title, [string]$Message)
    Add-Type -AssemblyName System.Windows.Forms
    $notify = New-Object System.Windows.Forms.NotifyIcon
    $notify.Icon = [System.Drawing.SystemIcons]::Information
    $notify.Visible = $true
    $notify.ShowBalloonTip(5000, $Title, $Message, [System.Windows.Forms.ToolTipIcon]::Info)
    Start-Sleep -Seconds 6
    $notify.Dispose()
}

Write-Output "Starting presence poller against root@$BoardIp (interval: ${PollIntervalSeconds}s). Ctrl+C to stop."

while ($true) {
    $state = ssh -o ConnectTimeout=3 -o BatchMode=yes root@$BoardIp "cat /tmp/presence_state 2>/dev/null" 2>$null
    $state = if ($state) { $state.Trim() } else { $null }

    if ($state -and $state -ne $lastState) {
        Write-Output "$(Get-Date -Format 'HH:mm:ss') state changed: $lastState -> $state"

        if ($state -eq "away") {
            rundll32.exe user32.dll,LockWorkStation
            Write-Output "  -> workstation locked"
        }

        $lastState = $state
    }

    $breakFlag = ssh -o ConnectTimeout=3 -o BatchMode=yes root@$BoardIp "test -f /tmp/break_reminder_flag && echo FLAGGED" 2>$null
    if ($breakFlag -match "FLAGGED") {
        Show-Notification -Title "Time for a break" -Message "You've been at your desk for a while - stand up and stretch."
        ssh -o ConnectTimeout=3 -o BatchMode=yes root@$BoardIp "rm -f /tmp/break_reminder_flag" 2>$null
    }

    Start-Sleep -Seconds $PollIntervalSeconds
}
