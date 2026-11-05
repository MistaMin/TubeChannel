$py = 'C:\Users\marco\AppData\Roaming\kimi-desktop\daimon-share\daimon\runtime\python\.venv\Scripts\python.exe'
$script = 'C:\Users\marco\Developer\plugin-qa-skills\skills\plugin-daw-test\scripts\reaper_qa.py'
$qa = 'C:\Users\marco\Developer\TubeChannel\qa\2026-11-04-quality-fix'
Start-Process -FilePath $py -ArgumentList @($script, 'launch', '--qa-dir', $qa) -WorkingDirectory 'C:\Users\marco\Developer\TubeChannel' -WindowStyle Hidden
