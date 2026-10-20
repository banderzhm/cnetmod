"""Record build interference without stopping other users' processes (Linux)."""
import json
from pathlib import Path
import re
import threading
import time


class QuietHost:
    def __init__(self, output):
        self.output = Path(output)
        self.events = []
        self.stop = threading.Event()
        self.thread = None

    def sample(self):
        found = []
        for path in Path('/proc').glob('[0-9]*/cmdline'):
            try:
                argv = path.read_bytes().replace(b'\0', b' ').decode(errors='replace')
                executable = Path(argv.split(' ', 1)[0]).name
                if (re.fullmatch(r'(clang(\+\+)?(-[0-9]+)?|cc1(plus)?|gcc|g\+\+|rustc|cargo|ninja|cmake|javac|golangci-lint)', executable)
                        or 'org.codehaus.plexus.classworlds.launcher.Launcher' in argv):
                    found.append({'pid': int(path.parent.name), 'argv': argv})
            except (OSError, ValueError):
                continue
        if found:
            self.events.append({'unix_time': time.time(), 'processes': found})
        return found

    def watch(self):
        while not self.stop.wait(.5):
            self.sample()

    def __enter__(self):
        if self.sample():
            self.save()
            raise RuntimeError('Build activity detected; wait before measuring, see ' + str(self.output))
        self.thread = threading.Thread(target=self.watch, daemon=True)
        self.thread.start()
        return self

    def save(self):
        self.output.write_text(json.dumps({'sample_interval_seconds': .5,
            'scope': 'Linux /proc compiler and build-tool activity; Windows and VM neighbours are not observable',
            'valid': not self.events, 'events': self.events}, indent=2))

    def __exit__(self, kind, value, traceback):
        self.stop.set()
        self.thread.join()
        self.sample()
        self.save()
        if kind is None and self.events:
            raise RuntimeError('Measurement contaminated by build activity: ' + str(self.output))
