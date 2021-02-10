#!/usr/bin/env python3

import containers.util as util


def list_image_names(uri):
    images = util.list_images(uri)

    # skip the "ci-" prefix each of our container images' name has
    names = [i["name"][3:] for i in images]
    names.sort()

    return names


def main():
    names = list_image_names(util.get_registry_uri(util.PROJECT_ID) + "?per_page=100")

    names_native = [name for name in names if "cross" not in name]
    names_cross = [name for name in names if "cross" in name]

    print("Available x86 container images:\n")
    print("\t" + "\n\t".join(names_native))

    if names_cross:
        print()
        print("Available cross-compiler container images:\n")
        print("\t" + "\n\t".join(names_cross))


if __name__ == "__main__":
    main()
