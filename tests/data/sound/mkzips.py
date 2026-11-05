import zipfile, sys, os
out = sys.argv[1]
DT = (2020, 1, 1, 0, 0, 0)
def mk(name, entries):
    with zipfile.ZipFile(os.path.join(out, name), 'w') as z:
        for n, data, comp in entries:
            zi = zipfile.ZipInfo(n, DT)
            zi.compress_type = comp
            if n.endswith('/'):
                zi.external_attr = (0o40755 << 16) | 0x10
            else:
                zi.external_attr = 0o644 << 16
            z.writestr(zi, data)
D, S = zipfile.ZIP_DEFLATED, zipfile.ZIP_STORED
mk('flat.zip', [
    ('001.sy1', 'Flat One\ncolor=red\nver=113\n0,1\n1,64\n', D),
    ('002.sy1', 'Flat Two\ncolor=blue\nver=112\n0,2\n', S),
    ('010.sy1', 'Flat Ten\r\ncolor=default\r\n5,7\r\n', D),
    ('readme.txt', 'flat bank readme\n', S),
])
mk('single.zip', [
    ('readme.txt', 'single readme\n', S),
    ('Inner Bank/', '', S),
    ('Inner Bank/001.sy1', 'Inner One\ncolor=green\nver=113\n3,33\n', D),
    ('Inner Bank/003.sy1', 'Inner Three\ncolor=cyan\nver=113\n3,99\n', D),
])
mk('nested.zip', [
    ('005.sy1', 'Top Five\ncolor=yellow\nver=113\n7,70\n', D),
    ('b/001.sy1', 'B One\ncolor=magenta\nver=113\n1,11\n', D),
    ('b/deep/001.sy1', 'Deep One\ncolor=red\nver=113\n2,22\n', D),
    ('b/deep/002.sy1', 'Deep Two\ncolor=blue\nver=113\n2,23\n', S),
    ('a/', '', S),
    ('a/002.sy1', 'A Two\ncolor=green\nver=113\n4,44\n', D),
])
mk('double.zip', [
    ('outer/inner/001.sy1', 'Double One\ncolor=red\nver=113\n9,90\n', D),
    ('outer/inner/002.sy1', 'Double Two\ncolor=blue\nver=113\n9,91\n', D),
])
mk('empty.zip', [])
with open(os.path.join(out, 'corrupt.zip'), 'w') as f:
    f.write('this is not a zip archive\n')
