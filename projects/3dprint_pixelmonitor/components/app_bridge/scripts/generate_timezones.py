"""Generate portable ESP32 tables from the checked-in Microsoft Windows export.

Refresh on Windows with scripts/export_windows_timezones.ps1, then run this file.
No external Python package, timezone conversion table or network access is used.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def fixed_rule(offset):
    value = abs(offset)
    return 'UTC' + ('-' if offset > 0 else '') + str(value // 3600) + ':' + f'{value // 60 % 60:02}:{value % 60:02}'


def generate():
    out = ROOT / 'resources/timezones'
    data = json.loads((out / 'windows-source.json').read_text(encoding='utf-8-sig'))
    groups, zones, definitions, rules = {}, {}, [], {}
    for zone in data['zones']:
        rows = tuple(tuple(row) for row in zone['transitions'])
        if not rows or rows[0][0] != data['fromEpoch']:
            raise ValueError('Invalid Windows transition export')
        key = (fixed_rule(rows[-1][1]), rows)
        index = groups.setdefault(key, len(groups))
        zones[zone['id']] = index
        rules[zone['id']] = fixed_rule(zone['offset'])
        definitions.append(dict(id=zone['id'], offset=zone['offset'], labels=zone['labels']))
    for alias, name in data['aliases'].items():
        if name in zones:
            zones[alias] = zones[name]
            rules[alias] = rules[name]
    (out / 'windows-zones.json').write_text(json.dumps(dict(zones=definitions, aliases=data['aliases'], rules=rules), ensure_ascii=False, separators=(',', ':')) + '\n', encoding='utf-8')
    lines = ['// Generated from Microsoft Windows timezone data; do not edit.', '#pragma once', '#include <cstdint>',
             'namespace bridge { namespace timezone_data {',
             'struct Transition { int64_t epoch; int32_t offset; };',
             'struct Rules { const char *tail; unsigned start, count; };',
             'struct Zone { const char *name; unsigned rules; };', 'static constexpr Transition transitions[] = {']
    for _, rows in groups:
        lines += ['{%dLL,%d},' % row for row in rows]
    lines += ['};', 'static constexpr Rules rules[] = {']
    start = 0
    for tail, rows in groups:
        lines.append('{%s,%d,%d},' % (json.dumps(tail), start, len(rows)))
        start += len(rows)
    lines += ['};', 'static constexpr Zone zones[] = {']
    lines += ['{%s,%d},' % (json.dumps(name), index) for name, index in sorted(zones.items())]
    lines += ['};', '} }', '']
    (ROOT / 'main/timezone_data.h').write_text('\n'.join(lines), encoding='utf-8')
    print(f"Windows {data['build']}: {len(definitions)} choices, {len(groups)} shared schedules, {start} transitions")


if __name__ == '__main__':
    generate()
