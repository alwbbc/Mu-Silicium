"""Generate a hivexsh script that registers hero2touch in an offline WinPE SYSTEM hive."""
import sys
PKG = 'hero2touch.inf_arm64_4e2f0b5523a7c911'
INF = 'hero2touch.inf'
CFG = 'Touch_Inst.NT'
fsize = int(sys.argv[1]) if len(sys.argv) > 1 else 0x8000

def msz(*items):
    b = b''.join(s.encode('utf-16-le') + b'\0\0' for s in items) + b'\0\0'
    return 'hex:7:' + ','.join('%02x' % x for x in b)
def hexv(t, data): return 'hex:%d:' % t + ','.join('%02x' % x for x in data)

out = []
def key(path, vals, create=()):
    # create subkeys along path (each listed component under its parent)
    parent = path.rsplit('\\', 1)[0]
    out.append('cd ' + parent)
    out.append('add ' + path.rsplit('\\', 1)[1])
    out.append('cd ' + path)
    if vals:
        out.append('setval %d' % len(vals))
        for n, v in vals:
            out.append(n); out.append(v)

version = bytes.fromhex('ffff0c0000000000') + bytes.fromhex('a0175a74d374d011b6fe00a0c90f57da') \
          + bytes.fromhex('00808ca3c594c601') + bytes([0,0, 0,0, 0,0, 1,0]) + bytes(8)
D = '\\DriverDatabase'
key(D + '\\DriverPackages\\' + PKG, [
    ('Version', hexv(3, version)), ('Provider', 'string:Project Silicium'),
    ('SignerScore', 'dword:0x0d000003'), ('FileSize', hexv(11, fsize.to_bytes(8, 'little'))),
    ('StatusFlags', 'dword:0x00000112'), ('@', 'string:' + INF)])
P = D + '\\DriverPackages\\' + PKG
key(P + '\\Configurations', [])
key(P + '\\Configurations\\' + CFG, [
    ('Service', 'string:mshidkmdf'), ('LowerFilters', msz('hero2touch')),
    ('ConfigScope', 'dword:0x00000007'), ('ConfigFlags', 'dword:0x00000000')])
key(P + '\\Descriptors', [])
key(P + '\\Descriptors\\ACPI', [])
key(P + '\\Descriptors\\ACPI\\HERO0552', [
    ('Configuration', 'string:' + CFG), ('Manufacturer', 'string:%prov%'), ('Description', 'string:%dev%')])
key(P + '\\Strings', [('prov', 'string:Project Silicium hero2lte'), ('dev', 'string:S6SA552 Touchscreen (hero2lte)')])
key(D + '\\DriverInfFiles\\' + INF, [('@', msz(PKG)), ('Active', 'string:' + PKG), ('Configurations', msz(CFG))])
key(D + '\\DeviceIds\\ACPI\\HERO0552', [(INF, hexv(3, bytes([1, 0xff, 0, 0])))])

S = '\\ControlSet001\\Services'
key(S + '\\hero2touch', [
    ('Type', 'dword:0x00000001'), ('Start', 'dword:0x00000003'), ('ErrorControl', 'dword:0x00000001'),
    ('ImagePath', 'expandstring:\\SystemRoot\\System32\\drivers\\hero2touch.sys'),
    ('DisplayName', 'string:S6SA552 Touchscreen (hero2lte)')])
key(S + '\\hero2touch\\Parameters', [])
key(S + '\\hero2touch\\Parameters\\Wdf', [('KmdfLibraryVersion', 'string:1.15')])
out.append('commit')
print('\n'.join(out))
