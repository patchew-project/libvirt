import os
import os.path
import re

srcdir = os.path.abspath((os.environ.get("srcdir", os.path.join("..", "src"))))

def get_allowed_functions():
    functions = []
    with open(os.path.join(srcdir, 'storage', 'storage_backend.h'), 'r') as handle:
        content = ''.join(handle.readlines())
        definition = re.search('struct _virStorageBackend {([^}]+)}', content)
        if definition is not None:
            functions = re.findall('virStorageBackend[^ ]+ ([^;]+)', definition.group(1))
    return functions

class Backend:
    def __init__(self, name, code):
        self.name = name
        self.functions = [member[1:] for member in re.findall('.([^ ]+) = ', code) if member != '.type']

def get_backends():
    backends = []
    for root, dirs, files in os.walk(os.path.join(srcdir, 'storage')):
        storage_impls = [os.path.join(root, f) for f in files if re.match('storage_backend_[^.]+.c', f)]
        for impl in storage_impls:
            handle = open(impl, 'r')
            content = ''.join(handle.readlines())
            handle.close()
            chunks = re.findall('virStorageBackend virStorageBackend([^ ]+) = {([^}]*)}', content)
            backends.extend([Backend(chunk[0], chunk[1]) for chunk in chunks])
    return backends

def main():
    functions = get_allowed_functions()
    backends = get_backends()

    headers = '\n'.join(['<th>%s</th>' % backend.name for backend in backends])
    rows = []
    for func in functions:
        cell_template = '<td style="text-align: center">%s</td>'
        support = [cell_template % ('&#10004;' if func in backend.functions else '') for backend in backends]
        rows.append('\n'.join(['<tr>', '<td>%s</td>' % func] + support + ['</tr>']))

    print('''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<body>
<table class='top_table'>
<thead>
<tr>
<th>
</th>
%s
</tr>
</thead>
<tbody>
%s
</tbody>
</table>
</body>
</html>''' % (headers, '\n'.join(rows)))

if __name__ == '__main__':
    main()
