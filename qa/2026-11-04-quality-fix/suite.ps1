$py = 'C:\Users\marco\AppData\Roaming\kimi-desktop\daimon-share\daimon\runtime\python\.venv\Scripts\python.exe'
$qa = 'C:\Users\marco\Developer\TubeChannel\qa\2026-11-04-quality-fix'
$exc = 'C:\Users\marco\Developer\TubeChannel\qa\exceptions.txt'
$out = Join-Path $qa 'suite.log'
$err = Join-Path $qa 'suite.err'
$argLine = 'C:\Users\marco\Developer\plugin-qa-skills\skills\plugin-daw-test\scripts\reaper_qa.py suite --qa-dir "{0}" --fx "VST3: Tube Channel" --exceptions "{1}"' -f $qa, $exc
Start-Process -FilePath $py -ArgumentList $argLine -WorkingDirectory 'C:\Users\marco\Developer\TubeChannel' -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err
