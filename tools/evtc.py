"""Minimal reader for arcdps .evtc/.zevtc logs (revision 1), per reference/writeencounter.cpp."""
import struct
import zipfile

EVENT = struct.Struct('<QQQiiIIHHHHBBBBBBBBBBBBBBBB')  # 64 bytes, cbtevent
FIELDS = ('time', 'src', 'dst', 'value', 'buff_dmg', 'overstack', 'skill', 'src_inst', 'dst_inst',
          'src_master', 'dst_master', 'iff', 'buff', 'result', 'activation', 'buff_remove', 'ninety', 'fifty',
          'moving', 'statechange', 'flanking', 'shields', 'offcycle', 'pad61', 'pad62', 'pad63', 'pad64')


def read(path):
    if path.endswith('.zevtc'):
        with zipfile.ZipFile(path) as z:
            data = z.read(z.namelist()[0])
    else:
        data = open(path, 'rb').read()
    assert data[:4] == b'EVTC', 'not an evtc file'
    build, revision, species = data[4:12].decode(), data[12], struct.unpack_from('<H', data, 13)[0]
    assert revision == 1, f'unsupported revision {revision}'
    pos = 16
    (agent_count,) = struct.unpack_from('<I', data, pos); pos += 4
    agents = {}
    for _ in range(agent_count):
        addr, prof, elite = struct.unpack_from('<QII', data, pos)
        raw = data[pos + 28:pos + 92]
        parts = raw.split(b'\0')
        agents[addr] = {
            'addr': addr, 'prof': prof, 'elite': elite, 'is_player': elite != 0xFFFFFFFF,
            'name': parts[0].decode('utf-8', 'replace'),
            'account': parts[1].decode('utf-8', 'replace') if len(parts) > 1 else '',
            'subgroup': parts[2].decode('utf-8', 'replace') if len(parts) > 2 else '',
        }
        pos += 96
    (skill_count,) = struct.unpack_from('<I', data, pos); pos += 4
    skills = {}
    for _ in range(skill_count):
        sid = struct.unpack_from('<i', data, pos)[0]
        skills[sid] = data[pos + 4:pos + 68].split(b'\0')[0].decode('utf-8', 'replace')
        pos += 68
    events = [dict(zip(FIELDS, EVENT.unpack_from(data, p))) for p in range(pos, len(data) - 63, 64)]
    return {'build': build, 'species': species, 'agents': agents, 'skills': skills, 'events': events}
