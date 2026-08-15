param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter(Mandatory = $true)]
    [string]$Screenshot
)

$ErrorActionPreference = 'Stop'

$process = Start-Process -FilePath $Executable -ArgumentList '--overlay-smoke' -PassThru
Start-Sleep -Seconds 2

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)

try {
    $graphics.CopyFromScreen(
        $bounds.Location,
        [System.Drawing.Point]::Empty,
        $bounds.Size
    )
    $bitmap.Save($Screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

$process.WaitForExit()
exit $process.ExitCode
