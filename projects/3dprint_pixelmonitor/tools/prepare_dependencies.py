"""Apply the project's reviewed patches to the pinned dependency checkouts."""
import json
from pathlib import Path
import subprocess


def main():
    project = Path(__file__).resolve().parents[1]
    workspace = project.parents[1]
    patches = project / 'patches'
    for item in json.loads((patches / 'manifest.json').read_text()):
        dependency = workspace / item['directory']
        actual = subprocess.check_output(['git', '-C', str(dependency), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != item['base']:
            raise SystemExit(f"Unexpected dependency revision: {item['directory']}: {actual}")
        command = ['git', '-C', str(dependency), 'apply']
        patch = str(patches / item['patch'])
        reverse = subprocess.run(command + ['--reverse', '--check', patch], capture_output=True)
        if reverse.returncode == 0:
            print(f"Already applied: {item['patch']}")
            continue
        subprocess.run(command + ['--check', patch], check=True)
        subprocess.run(command + [patch], check=True)
        print(f"Applied: {item['patch']}")


if __name__ == '__main__':
    main()
