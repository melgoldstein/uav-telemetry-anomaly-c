import csv, io, subprocess, sys, tempfile
result = subprocess.run([sys.argv[1],'data/flight_log_04.csv'],capture_output=True,text=True,check=True)
rows = list(csv.DictReader(io.StringIO(result.stdout)))
assert len(rows)==60
assert sum(int(row['quality_flag']) for row in rows)==6
with tempfile.NamedTemporaryFile(mode='w',suffix='.csv',delete=False) as stream:
    stream.write('sample,airspeed,altitude,hdop,snr\n1,nan,100,1,30\n'); path=stream.name
assert subprocess.run([sys.argv[1],path],capture_output=True).returncode != 0
from pathlib import Path
Path(path).unlink()
