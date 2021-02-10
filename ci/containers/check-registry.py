#!/usr/bin/env python3

import argparse
import subprocess
import shutil
import sys

import util


def get_image_distro(image_name: str):
    name_prefix = "ci-"
    name_suffix = "-cross-"

    distro = image_name[len(name_prefix):]

    index = distro.find(name_suffix)
    if index > 0:
        distro = distro[:index]

    return distro


def get_undesirables(registry_distros_d, hosts_l):
    """ Returns a dictionary of 'id':'name' pairs of images that can be purged"""

    undesirables_d = {}
    for distro in registry_distros_d:
        if distro not in hosts_l:
            for image in registry_distros_d[distro]:
                undesirables_d[str(image["id"])] = image["path"]

    return undesirables_d if undesirables_d else None


def get_lcitool_hosts(lcitool_path):
    """ Returns a list of supported hosts by lcitool
        @param lcitool_path: absolute path to local copy of lcitool, if omitted it is
        assumed lcitool is installed in the current environment"""

    if not lcitool_path:
        lcitool_path = "lcitool"
        if not shutil.which(lcitool_path):
            sys.exit("error: 'lcitool' not installed")

    lcitool_out = subprocess.check_output([lcitool_path, "hosts"])
    return lcitool_out.decode("utf-8").splitlines()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("project_id",
                        help="GitLab project ID")
    parser.add_argument("--lcitool_path",
                        dest="lcitool",
                        metavar="PATH",
                        help="absolute path to lcitool, CWD is used if omitted")

    args = parser.parse_args()

    uri = util.get_registry_uri(util.PROJECT_ID)
    images_json = util.list_images(uri + "?per_page=100")

    registry_distros_d = {}
    for image in images_json:
        distro_name = get_image_distro(image["name"])

        try:
            registry_distros_d[distro_name].append(image)
        except KeyError:
            registry_distros_d[distro_name] = [image]

    hosts_l = get_lcitool_hosts(args.lcitool)

    # print the list of images that we can safely purge from the registry
    undesirables_d = get_undesirables(registry_distros_d, hosts_l)
    if undesirables_d:
        undesirable_image_names = "\t" + "\n\t".join(undesirables_d.values())
        undesirable_image_ids = " ".join(undesirables_d.keys())

        sys.exit(f"""
The following images can be purged from the registry:

{undesirable_image_names}

You can remove the above images over the API with the following code snippet:

\t$ for image_id in {undesirable_image_ids} \\
\t;do \\
\t\tcurl --request DELETE --header "PRIVATE-TOKEN: <access_token>" \\
\t\t{util.get_registry_uri(util.PROJECT_ID)}/$image_id \\
\t;done""")


if __name__ == "__main__":
    main()
