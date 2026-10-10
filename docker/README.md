# Developing inside a docker container

In this tutorial, we explain how to develop, build and test this project inside a docker container.

The container user and password are:

**developer:developer**

## Prerequisites

First, you must install `docker`.

```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
```

## Build and start up a container

The container is defined in `docker-compose.yml`; `dock.sh` is a thin wrapper that supplies the container name, the host uid/gid and, when the rig's package cache (`stepit-apt-cache`) is running, its address, then calls `docker compose`.

Run this script to create an image and a container. It always mounts the repo it is part of, so it can be called from anywhere:

```bash
./docker/dock.sh dng-converter build
```

This is also how you pick up changes to the `Dockerfile`: it rebuilds only the layers that changed, so there is no need to `clean` first.

Run this to start the container with an interactive shell:

```bash
./docker/dock.sh dng-converter start
```

Or to run a single command in it, starting it if needed:

```bash
./docker/dock.sh dng-converter exec test.sh
```

Run this to stop the container:

```bash
./docker/dock.sh dng-converter stop
```

Finally, run this to remove container and image:

```bash
./docker/dock.sh dng-converter clean
```

## Working with the code

Inside the container, the repo is bind-mounted at `~/ws`. `~/ws/bin` is on the `PATH`, and the scripts are aliased, so `build`, `test` and `samples` work from any directory: they always act on the workspace root. The tool, `dng-convert`, is on the `PATH` once built.

The interactive shell setup -- the aliases -- lives in `docker/bashrc`, which the image installs as `~/.bashrc.dng` and sources from `~/.bashrc`. Variables belong in the `Dockerfile` as `ENV` instead, so that they apply to non-interactive commands too. Either way, rebuild the container afterwards to pick the change up.

## Our own raw files

The file `docker/.env`, which is not versioned, holds the settings of our own machine. `DNG_USER_SAMPLES` is the folder of raw files mounted read only at `~/user-samples`, which the tests convert along with the samples:

```bash
echo "DNG_USER_SAMPLES=$HOME/Pictures/cr2" > docker/.env
./docker/dock.sh dng-converter build
```

Without it, `~/user-samples` is the repo's `samples` folder again.
