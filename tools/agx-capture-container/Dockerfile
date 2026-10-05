FROM ubuntu:22.04@sha256:2edbbc5dc405e9612ba3584ce95480277e3eb374407b5505fe26f17df77c7dbc

ARG DEBIAN_FRONTEND=noninteractive
ARG MESA_URL=https://gitlab.freedesktop.org/asahilina/mesa.git
ARG MESA_COMMIT=7a4f24061fa56ef7eff12132dd7b1461d5a890d8

RUN set -eu; \
    packages="bison build-essential ca-certificates flex git meson ninja-build \
        pkg-config python3-dev python3-mako python3-pip python3-ply python3-pyelftools \
        python3-setuptools python3-wheel python3-yaml \
        libdrm-dev libelf-dev libexpat1-dev libglvnd-dev libudev-dev \
        libwayland-dev libx11-xcb-dev libxcb-dri2-0-dev libxcb-dri3-dev \
        libxcb-present-dev libxcb-shm0-dev libxdamage-dev libxext-dev \
        libxfixes-dev libxrandr-dev libxshmfence-dev libxxf86vm-dev \
        libzstd-dev socat wayland-protocols zlib1g-dev"; \
    attempt=1; \
    while :; do \
        rm -rf /var/lib/apt/lists/*; \
        if apt-get -o Acquire::Retries=5 update \
           && apt-get -o Acquire::Retries=5 install -y --no-install-recommends \
                $packages; then \
            break; \
        fi; \
        [ "$attempt" -lt 3 ] || exit 1; \
        attempt=$((attempt + 1)); \
        sleep 2; \
    done; \
    rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir construct==2.10.70 pyserial==3.5

RUN git init /opt/asahi-mesa \
    && git -C /opt/asahi-mesa remote add origin "${MESA_URL}" \
    && git -C /opt/asahi-mesa fetch --depth=1 origin "${MESA_COMMIT}" \
    && git -C /opt/asahi-mesa checkout --detach FETCH_HEAD \
    && test "$(git -C /opt/asahi-mesa rev-parse HEAD)" = "${MESA_COMMIT}"

RUN meson setup /opt/asahi-mesa/build /opt/asahi-mesa \
        -Dgallium-drivers=asahi \
        -Dvulkan-drivers= \
        -Dtools=drm-shim \
        -Dllvm=disabled \
        -Dglx=disabled \
        -Dplatforms= \
        -Degl=enabled \
        -Dgbm=enabled \
        -Dshared-glapi=enabled \
        -Dbuild-tests=false \
    && ninja -C /opt/asahi-mesa/build

COPY tools/agx_clear_capture.c /tmp/agx_clear_capture.c
COPY tools/verify-agx-capture-env.py /usr/local/bin/verify-agx-capture-env.py
COPY tools/agx-capture-container/run-capture.sh /opt/agx-capture/run-capture.sh

RUN mkdir -p /opt/agx-capture/bin /opt/agx-capture/export \
    && chmod +x /opt/agx-capture/run-capture.sh \
    && PKG_CONFIG_PATH=/opt/asahi-mesa/build/meson-uninstalled \
       cc -Wall -Wextra -Werror -O2 /tmp/agx_clear_capture.c \
          -o /opt/agx-capture/bin/agx-clear-capture \
          $(PKG_CONFIG_PATH=/opt/asahi-mesa/build/meson-uninstalled \
            pkg-config --cflags --libs egl glesv2) \
    && cp /opt/asahi-mesa/build/src/asahi/drm-shim/libasahi_m1n1_drm_shim.so \
          /opt/agx-capture/export/ \
    && cp /opt/asahi-mesa/build/src/egl/libEGL.so.1.0.0 \
          /opt/agx-capture/export/ \
    && cp /opt/asahi-mesa/build/src/mapi/es2api/libGLESv2.so.2.0.0 \
          /opt/agx-capture/export/ \
    && cp /opt/asahi-mesa/build/src/gallium/targets/dri/asahi_dri.so \
          /opt/agx-capture/export/ \
    && cp /opt/agx-capture/bin/agx-clear-capture /opt/agx-capture/export/ \
    && cp /usr/local/bin/verify-agx-capture-env.py /opt/agx-capture/export/ \
    && MESA_COMMIT="${MESA_COMMIT}" python3 -c \
       'import hashlib,json,os,pathlib,platform,subprocess; p=pathlib.Path("/opt/agx-capture/export"); names=("libasahi_m1n1_drm_shim.so","libEGL.so.1.0.0","libGLESv2.so.2.0.0","asahi_dri.so","agx-clear-capture"); h=lambda q: hashlib.sha256(q.read_bytes()).hexdigest(); data={"schema":1,"mesa_commit":os.environ["MESA_COMMIT"],"architecture":platform.machine(),"compiler":subprocess.check_output(["cc","--version"],text=True).splitlines()[0],"artifacts":{n:h(p/n) for n in names}}; (p/"manifest.json").write_text(json.dumps(data,sort_keys=True,indent=2)+"\n")' \
    && python3 /opt/agx-capture/export/verify-agx-capture-env.py \
          /opt/agx-capture/export

ENV LD_LIBRARY_PATH=/opt/asahi-mesa/build/src/egl:/opt/asahi-mesa/build/src/mapi/es2api
ENV LIBGL_DRIVERS_PATH=/opt/asahi-mesa/build/src/gallium/targets/dri
ENV MESA_LOADER_DRIVER_OVERRIDE=asahi

WORKDIR /work
