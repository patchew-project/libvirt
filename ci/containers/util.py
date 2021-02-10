import json
import urllib.request as urllib


PROJECT_ID = 192693


def get_registry_uri(project_id,
                     base_uri="https://gitlab.com",
                     api_version=4):
    uri = f"{base_uri}/api/v{str(api_version)}/projects/{project_id}/registry/repositories"
    return uri


def list_images(uri):
    """
    Returns all container images as currently available for the given GitLab
    project.

    ret: list of container image names
    """
    r = urllib.urlopen(uri)

    # read the HTTP response and load the JSON part of it
    return json.loads(r.read().decode())
