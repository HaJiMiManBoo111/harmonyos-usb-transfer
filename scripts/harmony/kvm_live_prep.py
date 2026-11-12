import hashlib, os

d = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'kvm-live')
os.makedirs(d, exist_ok=True)

def make(name, data):
    p = os.path.join(d, name)
    with open(p, 'wb') as f:
        f.write(data)
    print('FILE|%s|%d|%s|%s' % (
        name, len(data),
        hashlib.md5(data).hexdigest(),
        hashlib.sha256(data).hexdigest()))

small = bytes(((i * 73 + 19) & 0xFF) for i in range(4096))
multi = bytes(((i * 37 + 11) & 0xFF) for i in range(1536 * 1024))
make('kvm-live-small.bin', small)
make('kvm-live-multi.bin', multi)
print('PREP_DONE=1')
