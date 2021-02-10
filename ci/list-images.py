#!/usr/bin/env python3

import json
import urllib.request as urllib

PROJECT_ID = 192693
DOMAIN = "https://gitlab.com"
API = "api/v4"

uri = f"{DOMAIN}/{API}/projects/{PROJECT_ID}/registry/repositories"
r = urllib.urlopen(uri + "?per_page=100")

# read the HTTP response and load the JSON part of it
data = json.loads(r.read().decode())

# skip the "ci-" prefix each of our container images' name has
names = [i["name"][3:] for i in data]
names.sort()

names_native = [name for name in names if "cross" not in name]
names_cross = [name for name in names if "cross" in name]

print("Available x86 container images:\n")
print("\t" + "\n\t".join(names_native))

if names_cross:
    print()
    print("Available cross-compiler container images:\n")
    print("\t" + "\n\t".join(names_cross))
