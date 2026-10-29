"""Generate a hivexsh script that registers the hero2lte drivers in an offline
WinPE SYSTEM hive (DriverDatabase package + DeviceIds + service keys), the way
DISM /Add-Driver would, so PnP installs them at boot from the driver store.
usage: hero2lte_winpe_regs.py <touch_pkg_size> <ufs_pkg_size>"""
import sys

def msz(*items):
    b = b''.join(s.encode('utf-16-le') + b'\0\0' for s in items) + b'\0\0'
    return 'hex:7:' + ','.join('%02x' % x for x in b)
def hexv(t, data): return 'hex:%d:' % t + ','.join('%02x' % x for x in data)

out = []
def key(path, vals):
    parent, name = path.rsplit('\\', 1)
    out.append('cd ' + parent); out.append('add ' + name); out.append('cd ' + path)
    if vals:
        out.append('setval %d' % len(vals))
        for n, v in vals: out.append(n); out.append(v)

def guid_le(g):
    a, b, c, d, e = g.strip('{}').split('-')
    return (int(a, 16).to_bytes(4, 'little') + int(b, 16).to_bytes(2, 'little') +
            int(c, 16).to_bytes(2, 'little') + bytes.fromhex(d) + bytes.fromhex(e))

def package(pkg, inf, cfg, classguid, hwid, conf_vals, strings, size, provider):
    D = '\\DriverDatabase'
    version = bytes.fromhex('ffff0c0000000000') + guid_le(classguid) \
              + bytes.fromhex('00808ca3c594c601') + bytes([0, 0, 0, 0, 0, 0, 1, 0]) + bytes(8)
    P = D + '\\DriverPackages\\' + pkg
    key(P, [('Version', hexv(3, version)), ('Provider', 'string:' + provider),
            ('SignerScore', 'dword:0x0d000003'), ('FileSize', hexv(11, size.to_bytes(8, 'little'))),
            ('StatusFlags', 'dword:0x00000112'), ('@', 'string:' + inf)])
    key(P + '\\Configurations', [])
    key(P + '\\Configurations\\' + cfg, conf_vals + [('ConfigScope', 'dword:0x00000007'), ('ConfigFlags', 'dword:0x00000000')])
    key(P + '\\Descriptors', [])
    key(P + '\\Descriptors\\ACPI', [])
    key(P + '\\Descriptors\\ACPI\\' + hwid, [('Configuration', 'string:' + cfg),
            ('Manufacturer', 'string:%prov%'), ('Description', 'string:%dev%')])
    key(P + '\\Strings', [(k, 'string:' + v) for k, v in strings])
    key(D + '\\DriverInfFiles\\' + inf, [('@', msz(pkg)), ('Active', 'string:' + pkg), ('Configurations', msz(cfg))])
    key(D + '\\DeviceIds\\ACPI\\' + hwid, [(inf, hexv(3, bytes([1, 0xff, 0, 0])))])

def service(name, start, display, extra=()):
    S = '\\ControlSet001\\Services\\' + name
    key(S, [('Type', 'dword:0x00000001'), ('Start', 'dword:0x%08x' % start), ('ErrorControl', 'dword:0x00000001'),
            ('ImagePath', 'expandstring:\\SystemRoot\\System32\\drivers\\%s.sys' % name),
            ('DisplayName', 'string:' + display)] + list(extra))
    return S

touch_size = int(sys.argv[1]) if len(sys.argv) > 1 else 0x8000
ufs_size = int(sys.argv[2]) if len(sys.argv) > 2 else 0x8000

package('hero2touch.inf_arm64_4e2f0b5523a7c911', 'hero2touch.inf', 'Touch_Inst.NT',
        '{745a17a0-74d3-11d0-b6fe-00a0c90f57da}', 'HERO0552',
        [('Service', 'string:mshidkmdf'), ('LowerFilters', msz('hero2touch'))],
        [('prov', 'Project Silicium hero2lte'), ('dev', 'S6SA552 Touchscreen (hero2lte)')], touch_size, 'Project Silicium')
S = service('hero2touch', 3, 'S6SA552 Touchscreen (hero2lte)')
key(S + '\\Parameters', [])
key(S + '\\Parameters\\Wdf', [('KmdfLibraryVersion', 'string:1.15')])

if '--no-ufs' not in sys.argv:
    package('hero2ufs.inf_arm64_7a1c55e09b3d2f04', 'hero2ufs.inf', 'Ufs_Inst.NT',
            '{4D36E97B-E325-11CE-BFC1-08002BE10318}', 'HERO0155',
            [('Service', 'string:hero2ufs')],
            [('prov', 'Project Silicium hero2lte'), ('dev', 'Exynos 8890 UFS Host Controller (hero2lte)')], ufs_size, 'Project Silicium')
    S = service('hero2ufs', 3, 'Exynos 8890 UFS Host Controller (hero2lte)', [('Group', 'string:SCSI Miniport')])
    key(S + '\\Parameters', [('BusType', 'dword:0x00000013'), ('IoTimeoutValue', 'dword:0x0000001e')])

out.append('commit')
print('\n'.join(out))
