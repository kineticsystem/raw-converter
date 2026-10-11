# Ubuntu 24.04 with the tools to build and test raw-converter, for user
# "developer". Nothing is installed on the host: the repo is bind-mounted at
# ~/ws (see docker-compose.yml) and built from there.

FROM ubuntu:24.04

LABEL name="develop:raw-converter"

ENV DEBIAN_FRONTEND=noninteractive

# Every apt-get install refreshes the package index in the same layer: an
# index cached in a layer of its own goes stale as soon as Ubuntu publishes
# new versions, and then the next install fails, e.g. in CI, whose layer cache
# lasts for weeks.
RUN apt-get update && apt-get -y install sudo
RUN echo 'root:docker' | chpasswd                      # Update user root password.

# ubuntu:24.04 ships a default 'ubuntu' user at uid/gid 1000. Remove it so
# 'developer' can be created with a uid/gid matching the host user (passed in
# via build args), which keeps the bind-mounted workspace writable.
RUN if id -u ubuntu >/dev/null 2>&1; then userdel -r ubuntu; fi
RUN if getent group ubuntu >/dev/null 2>&1; then groupdel ubuntu; fi

ARG USER_UID=1000
ARG USER_GID=1000

# Create the user developer with a uid/gid matching the host user so files
# created in the bind-mounted workspace (the build directory, the downloaded
# samples and the converted DNG files) are owned correctly on the host.
RUN groupadd -g ${USER_GID} developer
RUN useradd --create-home --shell /bin/bash --uid ${USER_UID} --gid ${USER_GID} developer
RUN echo 'developer:developer' | chpasswd              # Update user developer password.
RUN usermod -aG sudo developer                          # Add user to sudoers.

# The C++ toolchain and the build system.
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake ninja-build pkg-config git

# LibRaw decodes and renders the camera RAW files (CR2 and every other format
# it knows); Little CMS makes the ICC profiles of the TIFF files.
RUN apt-get update && apt-get install -y --no-install-recommends libraw-dev liblcms2-dev

# Testing: GoogleTest for the unit tests; curl to download the sample RAW
# files; exiftool to inspect and validate the DNG files by hand and in the
# tests.
RUN apt-get update && apt-get install -y --no-install-recommends libgtest-dev curl ca-certificates libimage-exiftool-perl

# Formatting, the same major version of clang-format as the host.
RUN apt-get update && apt-get install -y --no-install-recommends clang-format pre-commit

# The container user is in the sudo group, but a password prompt makes the
# scripts in ./bin unusable from a non-interactive shell, e.g.
# `docker exec <container-name> build.sh`.
RUN echo 'developer ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/developer \
 && chmod 0440 /etc/sudoers.d/developer

CMD ["bash"]

USER developer

ENV HOME=/home/developer

# The repo is bind-mounted at ~/ws (see docker-compose.yml), so ./bin lives at
# a fixed path inside the container. Putting it on the PATH here rather than in
# the bashrc means it also applies to non-interactive shells, e.g.
# `docker exec <container-name> build.sh`. The converter itself is in
# build/apps.
ENV PATH="/home/developer/ws/bin:/home/developer/ws/build/apps/raw-convert:/home/developer/.local/bin:${PATH}"

# Whatever genuinely needs an interactive shell -- the aliases -- lives in this
# file, which is versioned in the repo instead of being echoed line by line
# into ~/.bashrc. Source it from the stock ~/.bashrc rather than replacing that
# file, to keep the distribution's own settings.
COPY --chown=developer:developer bashrc /home/developer/.bashrc.dng
RUN echo "source ~/.bashrc.dng" >> ~/.bashrc

WORKDIR /home/developer/ws
