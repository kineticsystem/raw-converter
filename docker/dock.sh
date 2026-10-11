#! /bin/bash -e

# Use this script to create, start, stop and remove a docker container to
# build and test raw-converter.
#
# The container itself is defined in docker-compose.yml. This script only adds
# what compose cannot express: the host uid/gid, the container name passed on
# the command line and, when it runs, the local package cache.

function container_exists() {
    local name="$1"
    [[ $(docker ps -aq --filter name=^/${name}$) ]]
}

function image_exists() {
    local name="$1"
    [[ $(docker images -q $name) ]]
}

function display_usage() {
    echo -e "\nUsage: ./dock.sh <container-name> <command> [options]\n
    Commands:
    build   Build a container without starting it
            Usage: ./dock.sh container-name build
    start   Start the container and open an interactive terminal
            Usage: ./dock.sh container-name start
    exec    Run a command in the container, starting it if needed
            Usage: ./dock.sh container-name exec build.sh
    stop    Stop the container
            Usage: ./dock.sh container-name stop
    clean   Stop the container and clean everything including images
            Usage: ./dock.sh container-name clean\n"
}

# The apt packages go through the rig's package cache (stepit-apt-cache, an
# apt-cacher-ng on port 3142) when it runs on this machine, so that they are
# downloaded only once. Without it, they are downloaded directly. The proxy is
# a predefined build argument: it does not invalidate the cached layers.
build_args=()
if (exec 3<>/dev/tcp/127.0.0.1/3142) 2>/dev/null; then
    build_args=(--build-arg http_proxy=http://127.0.0.1:3142)
fi

# Compose resolves the paths in docker-compose.yml against the directory that
# holds it, so every command has to run from there.
cd "$(dirname "$0")"

# Check for at least two arguments (container name and command).
if [ "$#" -lt 2 ]; then
    echo "Missing required arguments."
    display_usage
    exit 1
fi

name="$1"
command="$2"
shift 2

# The container name doubles as the compose project name, so several
# differently named containers can coexist from this same compose file.
export CONTAINER_NAME="$name"
export COMPOSE_PROJECT_NAME="$name"

# Build args for the Dockerfile: a container user matching the host user.
export USER_UID=$(id -u)
export USER_GID=$(id -g)

case "$command" in
    build)
        # Unlike a plain `docker build`, this rebuilds the layers that the
        # Dockerfile changed since last time, so there is no need to clean
        # first.
        docker compose build "${build_args[@]}"
        docker compose create
        ;;
    start)
        docker compose up --detach
        echo "Opening interactive terminal into $name"
        docker exec -it $name bash
        ;;
    exec)
        docker compose up --detach > /dev/null 2>&1
        docker exec $name bash -lc "$*"
        ;;
    stop)
        docker compose stop
        ;;
    clean)
        docker compose down --rmi local --remove-orphans
        if container_exists $name; then
            docker rm --force $name > /dev/null
        fi
        if image_exists $name:latest; then
            echo "Removing image: $name:latest"
            docker rmi $name:latest > /dev/null
        fi
        ;;
    *)
        echo "Unknown parameter: $command"
        display_usage
        ;;
esac
