# Helper script para flashear y monitorear la Kiwi Board en Windows PowerShell
$port = "COM3"
$python = "C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$idf_py = "C:\Espressif\frameworks\esp-idf-v5.5\tools\idf.py"

Write-Host "Flasheando a la Kiwi Board en $port..." -ForegroundColor Cyan
& $python $idf_py -p $port flash monitor
